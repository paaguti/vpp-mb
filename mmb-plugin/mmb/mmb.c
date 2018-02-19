/*
 * Copyright (c) 2015 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file
 * @brief MMB Plugin, plugin API / trace / CLI handling.
 * @author 
 */

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>

#include <mmb/mmb.h>
#include <mmb/mmb_format.h>
#include <mmb/mmb_classify.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

/* define message IDs */
#include <mmb/mmb_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <mmb/mmb_all_api_h.h> 
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <mmb/mmb_all_api_h.h> 
#undef vl_endianfun

/* instantiate all the print functions we know about */
#ifdef MMB_DEBUG
#  define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#else
#  define vl_print(handle, ...) 
#endif
#define vl_printfun
#include <mmb/mmb_all_api_h.h> 
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <mmb/mmb_all_api_h.h>
#undef vl_api_version

#define REPLY_MSG_ID_BASE mm->msg_id_base
#include <vlibapi/api_helper_macros.h>

#include <ctype.h>

/* List of message types that this plugin understands */
#define foreach_mmb_plugin_api_msg

/* internal macros */
#define MMB_MAX_FIELD_LEN 64
#define MMB_DEFAULT_ETHERNET_TYPE ETHERNET_TYPE_IP4
#define MMB_MATCH_IP_VERSION

#define vec_insert_elt_last(V,E) vec_insert_elts(V,E,1,vec_len(V))
#define vec_insert_elt(V,E,I) vec_insert_elts(V,E,1,I)

/* *INDENT-OFF* */
VLIB_PLUGIN_REGISTER () = {
    .version = MMB_PLUGIN_BUILD_VER,
    .description = "Modular Middlebox",
};
/* *INDENT-ON* */

const u8 fields_len = 70;
const char* fields[] = {
  "in", "out",
  "net-proto", "ip-ver", "ip-ihl",
  "ip-dscp", "ip-ecn", "ip-non-ect",
  "ip-ect0", "ip-ect1", "ip-ce",
  "ip-len", "ip-id", "ip-flags",
  "ip-res", "ip-df", "ip-mf",
  "ip-frag-offset", "ip-ttl", "ip-proto",
  "ip-checksum", "ip-saddr", "ip-daddr",
  "ip-payload",

  "ip6-ver", "ip6-traffic-class", "ip6-flow-label",
  "ip6-len", "ip6-next", "ip6-hop-limit",
  "ip6-saddr", "ip6-daddr", "ip6-payload",          /* 10 */

  "icmp-type", "icmp-code", "icmp-checksum",
  "icmp-payload", "udp-sport", "udp-dport",
  "udp-len", "udp-checksum", "udp-payload", 
  "tcp-sport", "tcp-dport", "tcp-seq-num", 
  "tcp-ack-num", "tcp-offset", "tcp-reserved",
  "tcp-urg-ptr", "tcp-cwr", "tcp-ece", 
  "tcp-urg", "tcp-ack", "tcp-push", 
  "tcp-rst", "tcp-syn", "tcp-fin", 
  "tcp-flags", "tcp-win", "tcp-checksum", 
  "tcp-payload",  "tcp-opt-mss", "tcp-opt-wscale",      /* 20 */
  "tcp-opt-sackp", "tcp-opt-sack", "tcp-opt-timestamp", 
  "tcp-opt-fast-open", "tcp-opt-mptcp", "tcp-opt",
  "all"
};

const u8 lens[] = {
  4, 4,
  2, 1, 1,
  1, 1, 1,
  1, 1, 1,
  2, 2, 1,
  1, 1, 1,
  2, 1, 1,
  2, 5, 5,
  0,
  1, 1, 3,
  2, 1, 1,
  17,17,0,   /* 10 */

  1, 1, 2,
  0, 2, 2,
  2, 2, 0, 
  2, 2, 4,
  4, 1, 1,
  2, 1, 1, 
  1, 1, 1, 
  1, 1, 1, 
  1, 2, 2, 
  0, 2, 1,  /* 20 */
  0, 0, 8, 
  0, 0, 0,
  0
};

const u8 fixed_len[] = {
  1, 1,
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  0,
  1, 1, 1,
  1, 1, 1,
  1, 1, 0,  /* 10 */
  1, 1, 1,
  0, 1, 1,
  1, 1, 0, 
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  1, 1, 1, 
  1, 1, 1, 
  1, 1, 1,
  0, 1, 1, /* 20 */
  1, 0, 1, 
  0, 0, 0,
  1
};

const u8 conditions_len = 6;
const char* conditions[] = {"==", "!=", "<=", ">=", "<", ">"};

static void free_rule(mmb_rule_t *rule);
static void init_rule(mmb_rule_t *rule);
static clib_error_t* parse_rule(unformat_input_t * input, 
                                                 mmb_rule_t *rule);
static clib_error_t* validate_rule();
static clib_error_t* validate_matches(mmb_rule_t *rule);
static clib_error_t* validate_targets(mmb_rule_t *rule);
static clib_error_t* mmb_enable_disable_fn(vlib_main_t * vm,
                                           unformat_input_t * input,
                                           vlib_cli_command_t * cmd,
                                           u32 *sw_if_index);
static int mmb_add_del_session(u32 table_index, u8 *key, u32 next_node, 
                               u32 rule_index, int is_add);
static int mmb_classify_del_table(u32 *table_index, int del_chain);
static void attach_table_intfc(u32 table_index, int is_add);

static_always_inline u8 rule_has_tcp_options(mmb_rule_t *rule)
{
  return rule->opts_in_matches || rule->opts_in_targets;
}

static_always_inline void reset_flags(mmb_main_t *mm) {
   mm->opts_in_rules = 0;
} 

static_always_inline void update_flags(mmb_main_t *mm, mmb_rule_t *rules) {
   mmb_rule_t *rule;
   vec_foreach(rule, rules) {
      if (rule_has_tcp_options(rule)) {
          mm->opts_in_rules = 1;
          return;
      }
   }
   mm->opts_in_rules = 0;
} 

static_always_inline u32 bytes_to_u32(u8 *bytes) {
  u32 value = 0;
  u32 index = 0;
  const u32 len = clib_min(3,vec_len(bytes)-1);

  vec_foreach_index(index, bytes) {
    value += ((u32) bytes[index]) << (len-index)*8;
    if (index==len) break;
  }

  return value;
}

static_always_inline u64 bytes_to_u64(u8 *bytes) {
  u64 value = 0;
  u32 index = 0;
  const u32 len = clib_min(7,vec_len(bytes)-1);

  vec_foreach_index(index, bytes) {
    value += ((u64) bytes[index]) << ((len-index)*8);
    if (index==len) break;
  }

  return value;
}

static_always_inline void mmb_enable_disable(u32 sw_if_index, int enable_disable) {
   mmb_classify_main_t *mcm = &mmb_classify_main;
   vnet_feature_enable_disable("ip4-unicast", "ip4-mmb-rewrite", 
                               sw_if_index, enable_disable, 0, 0);
   vnet_feature_enable_disable("ip6-unicast", "ip6-mmb-rewrite", 
                               sw_if_index, enable_disable, 0, 0);

  if (enable_disable) {
     u32 ti;
     for (ti = 0; ti < MMB_CLASSIFY_N_TABLES; ti++)
         vec_validate_init_empty
           (mcm->classify_table_index_by_sw_if_index[ti], sw_if_index, ~0);
   }

   vnet_feature_enable_disable("ip4-unicast", "ip4-mmb-classify",
		      sw_if_index, enable_disable, 0, 0);
   vnet_feature_enable_disable("ip6-unicast", "ip6-mmb-classify",
		      sw_if_index, enable_disable, 0, 0);
}

static_always_inline void mmb_enable_disable_all(int enable_disable) {
   mmb_main_t *mm = &mmb_main;
   u32 *sw_if_index;
   vec_foreach(sw_if_index, mm->sw_if_indexes) {
      mmb_enable_disable(*sw_if_index, enable_disable);
   }
   mm->enabled = enable_disable;
}

clib_error_t* mmb_enable_disable_fn(vlib_main_t * vm,
                                    unformat_input_t * input,
                                    vlib_cli_command_t * cmd,
                                    u32 *sw_if_index) {
  unformat_input_tolower(input);
  mmb_main_t *mm = &mmb_main;
  *sw_if_index = ~0;
  
  while(unformat_check_input(input) != UNFORMAT_END_OF_INPUT) {
    if (!unformat(input, "%U", unformat_vnet_sw_interface, 
                  mm->vnet_main, sw_if_index))
      break;
  }

  if (*sw_if_index == ~0)
    return clib_error_return(0, "Please specify an interface...");

  /* Utterly wrong? */
  if (pool_is_free_index (mm->vnet_main->interface_main.sw_interfaces, 
                          *sw_if_index))
    return clib_error_return(0, "Invalid interface, only works on "
                                 "physical ports");
  return 0;
}

static clib_error_t *
enable_command_fn(vlib_main_t * vm,
                  unformat_input_t * input,
                  vlib_cli_command_t * cmd) {
   u8 index;
   u32 sw_if_index, enabled_sw_if_index;
   mmb_main_t *mm = &mmb_main;
   clib_error_t *error;

   if ( (error = mmb_enable_disable_fn(vm, input, cmd, &sw_if_index)) )
     return error;

   /* if already enabled ? */
   vec_foreach_index(index, mm->sw_if_indexes) {
      enabled_sw_if_index = mm->sw_if_indexes[index];
      if (sw_if_index == enabled_sw_if_index) 
         return clib_error_return(0, "mmb is already enabled on %U\n", 
                       format_vnet_sw_if_index_name, 
                       mm->vnet_main, sw_if_index);
   }

   vec_add1(mm->sw_if_indexes, sw_if_index);
   if (mm->enabled)
      mmb_enable_disable(sw_if_index, 1);
   vlib_cli_output(vm, "mmb enabled on %U\n", format_vnet_sw_if_index_name, 
             mm->vnet_main, sw_if_index);

   return 0;
}

static clib_error_t*
disable_command_fn(vlib_main_t * vm,
                   unformat_input_t * input,
                   vlib_cli_command_t * cmd) {
   u8 enabled = 0, index = 0;
   u32 sw_if_index, enabled_sw_if_index;
   mmb_main_t *mm = &mmb_main;
   clib_error_t *error;

   if ( (error = mmb_enable_disable_fn(vm, input, cmd, &sw_if_index)) )
     return error;

   /* if already disabled ? */
   vec_foreach_index(index, mm->sw_if_indexes) {
      enabled_sw_if_index = mm->sw_if_indexes[index];
      if (sw_if_index == enabled_sw_if_index) {
         enabled = 1;
         break;
      }
   }
   if (!enabled)
      return clib_error_return(0, "mmb is not enabled on %U\n", 
              format_vnet_sw_if_index_name, 
              mm->vnet_main, sw_if_index);

   vec_delete(mm->sw_if_indexes, 1, index);
   mmb_enable_disable(sw_if_index, 0); /* TODO: del related tables */
   vlib_cli_output(vm, "mmb disabled on %U\n", format_vnet_sw_if_index_name, 
          mm->vnet_main, sw_if_index);

  return 0;
}

static clib_error_t*
list_rules_command_fn(vlib_main_t * vm,
                          unformat_input_t * input,
                          vlib_cli_command_t * cmd) {
  mmb_main_t *mm = &mmb_main;

  if (!unformat_is_eof(input))
    return clib_error_return(0, "Syntax error: unexpected additional element");
  
  vlib_cli_output(vm, "%U", mmb_format_rules, mm->rules);

  return 0;
}

static clib_error_t*
flush_rules_command_fn(vlib_main_t * vm,
                        unformat_input_t * input,
                        vlib_cli_command_t * cmd) {
  unformat_input_tolower(input);
  if (!unformat_is_eof(input))
    return clib_error_return(0, "Syntax error: unexpected additional element");

  mmb_main_t *mm = &mmb_main;
  mmb_rule_t *rules = mm->rules;
  uword rule_index;
  u32 first_table_index = ~0;

  if (vec_len(mm->tables) == 0)
     return 0;

  first_table_index = mm->tables[0].index;
  attach_table_intfc(first_table_index, 0);

  vec_foreach_index(rule_index, rules) {
    mmb_rule_t *rule = &rules[rule_index];
    mmb_add_del_session(rule->classify_table_index, rule->classify_key, 
                        0, 0, 0); 
    free_rule(rule);
  }

  mmb_classify_del_table(&first_table_index, 1);
  vec_delete(mm->tables, vec_len(mm->tables), 0);

  if (vec_len(rules))
    vec_delete(rules, vec_len(rules), 0);

   if (mm->enabled) 
      mmb_enable_disable_all(0);

  reset_flags(mm);

  return 0;
}

static clib_error_t*
insert_rule_command_fn(vlib_main_t * vm,
                     unformat_input_t * input,
                     vlib_cli_command_t * cmd) {
   unformat_input_tolower(input);
   u32 rule_index;
   mmb_main_t *mm = &mmb_main;
   mmb_rule_t *rules = mm->rules;

   if (unformat(input, "%u", &rule_index)) {
      /* oow index */
      if (rule_index <= 0)
         rule_index = 0;
      else if (rule_index > vec_len(rules))
         rule_index = vec_len(rules);
      else 
         rule_index--;
      
      mmb_rule_t rule;
      clib_error_t *error;
      init_rule(&rule);

      if ( (error = parse_rule(input, &rule)) )
         return error;
      /* XXX: rebuild classifier: if previous or next table has same mask, 
         than this rule,
         add new session to it, 
         else:
             - if previous and next table have different mask or no previous or no next
               new table, new session, rechain tables

             - if previous and next have same mask, but different than this rule,
               PICK ONE: - error message, cannot insert in the middle of a table
                         - split table
         
        */
      vec_insert_elt(mm->rules,&rule,rule_index);
      
      /* flags */
      if (rule_has_tcp_options(&rule))
         mm->opts_in_rules = 1;

      if (!mm->enabled) 
         mmb_enable_disable_all(1);

      vlib_cli_output(vm, "Inserted rule at index %u: %U", 
               rule_index+1, mmb_format_rule, &rule);
      return 0;
   } 
   return clib_error_return(0, 
    "Syntax error: rule number must be an integer greater than 0");
}

static int vnet_set_mmb_classify_intfc(vlib_main_t *vm, u32 sw_if_index,
                                  u32 ip4_table_index, u32 ip6_table_index,
                                  u32 is_add) {

  mmb_classify_main_t *mcm = &mmb_classify_main;
  vnet_classify_main_t *vcm = mcm->vnet_classify_main;
  u32 pct[MMB_CLASSIFY_N_TABLES] = {ip4_table_index, ip6_table_index};
  u32 ti;

  /* Assume that we've validated sw_if_index in the API layer */

  for (ti = 0; ti < MMB_CLASSIFY_N_TABLES; ti++)
    {
      if (pct[ti] == ~0)
        continue;

      if (pool_is_free_index (vcm->tables, pct[ti]))
        return VNET_API_ERROR_NO_SUCH_TABLE;

      vec_validate_init_empty
        (mcm->classify_table_index_by_sw_if_index[ti], sw_if_index, ~0);

      /* Reject any DEL operation with wrong sw_if_index */
      if (!is_add &&
          (pct[ti] != mcm->classify_table_index_by_sw_if_index[ti][sw_if_index]))
        {
          clib_warning ("Non-existent intf_idx=%d with table_index=%d for delete",
                        sw_if_index, pct[ti]);
          return VNET_API_ERROR_NO_SUCH_TABLE;
        }
      /* Return ok on ADD operaton if feature is already enabled */
      if (is_add &&
          mcm->classify_table_index_by_sw_if_index[ti][sw_if_index] == pct[ti])
          return 0;

      if (is_add)
        mcm->classify_table_index_by_sw_if_index[ti][sw_if_index] = pct[ti];
      else
        mcm->classify_table_index_by_sw_if_index[ti][sw_if_index] = ~0;
    }

  return 0;
}

static void icmp46_header_host_to_net(icmp46_header_t *icmp) {
   icmp->checksum = clib_host_to_net_u16(icmp->checksum);
}

static void tcp_header_host_to_net(tcp_header_t *tcp) {
   tcp->src_port = clib_host_to_net_u16(tcp->src_port);
   tcp->dst_port = clib_host_to_net_u16(tcp->dst_port);
   tcp->seq_number = clib_host_to_net_u32(tcp->seq_number);
   tcp->ack_number = clib_host_to_net_u32(tcp->ack_number);
   tcp->window = clib_host_to_net_u16(tcp->window);
   tcp->checksum = clib_host_to_net_u16(tcp->checksum);
   tcp->urgent_pointer = clib_host_to_net_u16(tcp->urgent_pointer);
}

static void udp_header_host_to_net(udp_header_t *udp) {
   udp->src_port = clib_host_to_net_u16(udp->src_port);
   udp->dst_port = clib_host_to_net_u16(udp->dst_port);
   udp->length = clib_host_to_net_u16(udp->length);
   udp->checksum = clib_host_to_net_u16(udp->checksum);
}

static void ip4_header_host_to_net(ip4_header_t *ip) {
   ip->length = clib_host_to_net_u16(ip->length);
   ip->fragment_id = clib_host_to_net_u16(ip->fragment_id);
   ip->flags_and_fragment_offset = 
     clib_host_to_net_u16(ip->flags_and_fragment_offset);
   ip->checksum = clib_host_to_net_u16(ip->checksum);
   ip->src_address.as_u32 = clib_host_to_net_u32(ip->src_address.as_u32); 
   ip->dst_address.as_u32 = clib_host_to_net_u32(ip->dst_address.as_u32);
}

static void ip6_header_host_to_net(ip6_header_t *ip) {
   ip->ip_version_traffic_class_and_flow_label = 
     clib_host_to_net_u32(ip->ip_version_traffic_class_and_flow_label);
   ip->payload_length = clib_host_to_net_u16(ip->payload_length);
   ip->src_address.as_u64[0] = clib_host_to_net_u64(ip->src_address.as_u64[0]); 
   ip->src_address.as_u64[1] = clib_host_to_net_u64(ip->src_address.as_u64[1]); 
   ip->dst_address.as_u64[0] = clib_host_to_net_u64(ip->dst_address.as_u64[0]); 
   ip->dst_address.as_u64[1] = clib_host_to_net_u64(ip->dst_address.as_u64[1]); 
}

static_always_inline void mmb_icmp_mask_and_key_inline(icmp46_header_t *icmp_mask,
     icmp46_header_t *icmp_key, u8 field, u8 *value) {

   switch (field) {
      case MMB_FIELD_ICMP_TYPE:
         icmp_mask->type = 0xff;
         icmp_key->type = *value;
         break;
      case MMB_FIELD_ICMP_CODE:
         icmp_mask->code = 0xff;
         icmp_key->code = *value;
         break;
      case MMB_FIELD_ICMP_CHECKSUM:
         icmp_mask->checksum = 0xffff;
         icmp_key->checksum = bytes_to_u32(value);
         break;
      case MMB_FIELD_ICMP_PAYLOAD:
         /* XXX */
         break;
      default:
         break;
    }
}

static void mmb_icmp_mask_and_key(mmb_rule_t *rule, u8 *mask, u8 *key, u8 offset,
                                int is_match) {

  icmp46_header_t *icmp_mask = (icmp46_header_t *) (mask+offset);
  icmp46_header_t *icmp_key = (icmp46_header_t *) (key+offset);

  if (is_match) {
     mmb_match_t *match;
     vec_foreach(match, rule->matches) {
       mmb_icmp_mask_and_key_inline(icmp_mask, icmp_key, match->field, match->value);
     }
  } else {
     mmb_target_t *target;
     vec_foreach(target, rule->targets) {
       mmb_icmp_mask_and_key_inline(icmp_mask, icmp_key, target->field, target->value);
     }
  }

  icmp46_header_host_to_net(icmp_mask);
  icmp46_header_host_to_net(icmp_key);
}

static_always_inline void mmb_udp_mask_and_key_inline(udp_header_t *udp_mask, 
   udp_header_t *udp_key, u8 field, u8 *value) {

  switch (field) {
      case MMB_FIELD_UDP_SPORT:
         udp_mask->src_port = 0xffff;
         udp_key->src_port = bytes_to_u32(value);
         break;
      case MMB_FIELD_UDP_DPORT:
         udp_mask->dst_port = 0xffff;
         udp_key->dst_port = bytes_to_u32(value);
         break;
      case MMB_FIELD_UDP_LEN:
         udp_mask->length = 0xffff;
         udp_key->length = bytes_to_u32(value);
         break;
      case MMB_FIELD_UDP_CHECKSUM:
         udp_mask->checksum = 0xffff;
         udp_key->checksum = bytes_to_u32(value);
         break;
      case MMB_FIELD_UDP_PAYLOAD:
         /* XXX */
         break;
      default:
         break;
    }
}

static void mmb_udp_mask_and_key(mmb_rule_t *rule, u8 *mask, u8 *key, 
                               u8 offset, int is_match) {

  udp_header_t *udp_mask = (udp_header_t *) (mask+offset);
  udp_header_t *udp_key = (udp_header_t *) (key+offset);

   if (is_match) {
     mmb_match_t *match;
     vec_foreach(match, rule->matches) {
       mmb_udp_mask_and_key_inline(udp_mask, udp_key, match->field, match->value);
     }
  } else {
     mmb_target_t *target;
     vec_foreach(target, rule->targets) {
       mmb_udp_mask_and_key_inline(udp_mask, udp_key, target->field, target->value);
     }
  }

  udp_header_host_to_net(udp_mask);
  udp_header_host_to_net(udp_key);
}

static_always_inline void mmb_tcp_mask_and_key_inline(tcp_header_t *tcp_mask, 
     tcp_header_t *tcp_key, u8 field, u8 *value) {

  switch (field) {
      case MMB_FIELD_TCP_SPORT:
         tcp_mask->src_port = 0xffff;
         tcp_key->src_port = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_DPORT:
         tcp_mask->dst_port = 0xffff;
         tcp_key->dst_port = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_SEQ_NUM:
         tcp_mask->seq_number = 0xffffffff;
         tcp_key->seq_number = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_ACK_NUM:
         tcp_mask->ack_number = 0xffffffff;
         tcp_key->ack_number = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_OFFSET:
         tcp_mask->data_offset_and_reserved |= 0xf0;
         tcp_key->data_offset_and_reserved |= (*value << 4);
         break;
      case MMB_FIELD_TCP_RESERVED:
         tcp_mask->data_offset_and_reserved |= 0x0f;
         tcp_key->data_offset_and_reserved |= *value;
         break;
      case MMB_FIELD_TCP_FLAGS: 
         tcp_mask->flags = 0xff;
         tcp_key->flags |= *value;
         break;
      case MMB_FIELD_TCP_CWR:
         tcp_mask->flags |= TCP_FLAG_CWR;
         if (*value)
           tcp_key->flags |= TCP_FLAG_CWR;
         break;
      case MMB_FIELD_TCP_ECE:
         tcp_mask->flags |= TCP_FLAG_ECE;
         if (*value)
           tcp_key->flags |= TCP_FLAG_ECE;
         break;
      case MMB_FIELD_TCP_URG:
         tcp_mask->flags |= TCP_FLAG_URG;
         if (*value)
           tcp_key->flags |= TCP_FLAG_URG;
         break;
      case MMB_FIELD_TCP_ACK:
         tcp_mask->flags |= TCP_FLAG_ACK;
         if (*value)
           tcp_key->flags |= TCP_FLAG_ACK;
         break;
      case MMB_FIELD_TCP_PUSH:
         tcp_mask->flags |= TCP_FLAG_PSH;
         if (*value)
           tcp_key->flags |= TCP_FLAG_PSH;
         break;
      case MMB_FIELD_TCP_RST:
         tcp_mask->flags |= TCP_FLAG_RST;
         if (*value)
           tcp_key->flags |= TCP_FLAG_RST;
         break;
      case MMB_FIELD_TCP_SYN:
         tcp_mask->flags |= TCP_FLAG_SYN;
         if (*value)
           tcp_key->flags |= TCP_FLAG_SYN;
         break;
      case MMB_FIELD_TCP_FIN:
         tcp_mask->flags |= TCP_FLAG_FIN;
         if (*value)
           tcp_key->flags |= TCP_FLAG_FIN;
         break;
      case MMB_FIELD_TCP_WINDOW:
         tcp_mask->window = 0xffff;
         tcp_key->window = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_CHECKSUM:
         tcp_mask->checksum = 0xffff;
         tcp_key->checksum = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_URG_PTR:
         tcp_mask->urgent_pointer = 0xffff;
         tcp_key->urgent_pointer = bytes_to_u32(value);
         break;
      case MMB_FIELD_TCP_PAYLOAD:
         /* XXX */
         break;
      default:
         break;
    }
}

static void mmb_tcp_mask_and_key(mmb_rule_t *rule, u8 *mask, 
              u8 *key, u8 offset, int is_match) {
 
  tcp_header_t *tcp_mask = (tcp_header_t *) (mask+offset);
  tcp_header_t *tcp_key = (tcp_header_t *) (key+offset);

  if (is_match) {
     mmb_match_t *match;
     vec_foreach(match, rule->matches) {
       mmb_tcp_mask_and_key_inline(tcp_mask, tcp_key, match->field, match->value);
     }
  } else {
     mmb_target_t *target;
     vec_foreach(target, rule->targets) {
       mmb_tcp_mask_and_key_inline(tcp_mask, tcp_key, target->field, target->value);
     }
  }

  tcp_header_host_to_net(tcp_mask);
  tcp_header_host_to_net(tcp_key);
}

static_always_inline void mmb_ip4_mask_and_key_inline(ip4_header_t *ip_mask, 
     ip4_header_t *ip_key, u8 field, u8 *value) {

  switch (field) {    
   case MMB_FIELD_IP4_VER:
     ip_mask->ip_version_and_header_length |= 0xf0;
     ip_key->ip_version_and_header_length |= (*value << 4);
     break;
   case MMB_FIELD_IP4_IHL:
      ip_mask->ip_version_and_header_length |= 0x0f;
      ip_key->ip_version_and_header_length |= *value;
      break;
   case MMB_FIELD_IP4_DSCP:
      ip_mask->tos |= 0xfc;
      ip_key->tos |= (*value << 2);
      break;
   case MMB_FIELD_IP4_ECN:
      ip_mask->tos |= 0x03;
      ip_key->tos |= *value;
      break;
   case MMB_FIELD_IP4_LEN:
      ip_mask->length = 0xffff;
      ip_key->length = bytes_to_u32(value);
      break;
   case MMB_FIELD_IP4_ID:
      ip_mask->fragment_id = 0xffff;
      ip_key->fragment_id = bytes_to_u32(value);
      break;
   case MMB_FIELD_IP4_FLAGS:
      ip_mask->flags_and_fragment_offset |= 0xc000;
      ip_key->flags_and_fragment_offset |= (*value << 13);
      break;
   case MMB_FIELD_IP4_FRAG_OFFSET:
      ip_mask->flags_and_fragment_offset |= 0x1fff;
      ip_key->flags_and_fragment_offset |= bytes_to_u32(value);
      break;
   case MMB_FIELD_IP4_RES: /* congestion ?? */
      ip_mask->flags_and_fragment_offset |= IP4_HEADER_FLAG_CONGESTION;
      if (*value)
         ip_key->flags_and_fragment_offset |= IP4_HEADER_FLAG_CONGESTION;
      break;
   case MMB_FIELD_IP4_DF:
      ip_mask->flags_and_fragment_offset |= IP4_HEADER_FLAG_DONT_FRAGMENT;
      if (*value)
        ip_key->flags_and_fragment_offset |= IP4_HEADER_FLAG_DONT_FRAGMENT;
      break;
   case MMB_FIELD_IP4_MF:
      ip_mask->flags_and_fragment_offset |= IP4_HEADER_FLAG_MORE_FRAGMENTS;
      if (*value)
        ip_key->flags_and_fragment_offset |= IP4_HEADER_FLAG_MORE_FRAGMENTS;
      break;
   case MMB_FIELD_IP4_TTL:
      ip_mask->ttl = 0xff;
      ip_key->ttl = *value;
      break;
   case MMB_FIELD_IP4_PROTO:
      ip_mask->protocol = 0xff;
      ip_key->protocol = *value;
      break;
   case MMB_FIELD_IP4_CHECKSUM:
      ip_mask->checksum = 0xffff;
      ip_key->checksum = bytes_to_u32(value);
      break;
   case MMB_FIELD_IP4_SADDR:
      /* subnet mask, corrected subnet value */
      ip_mask->src_address.as_u32 = 0xffffffff << (32-value[4]); 
      ip_key->src_address.as_u32 = bytes_to_u32(value) & ip_mask->src_address.as_u32;
      break;
   case MMB_FIELD_IP4_DADDR:
      ip_mask->dst_address.as_u32 = 0xffffffff << (32-value[4]);
      ip_key->dst_address.as_u32 = bytes_to_u32(value) & ip_mask->dst_address.as_u32;
      break;
   case MMB_FIELD_IP4_PAYLOAD:
      /* XXX */
      break;
   default:
      break;
 }
}

static void mmb_ip4_mask_and_key(mmb_rule_t *rule, u8 *mask, u8 *key, int is_match) {
 /* ip4
  * 0: ip-ver, ip-ihl, ip-dscp, ip-ecn, ip-len, ip-id, ip-flags, ip-res,
  *    ip-df, ip-mf, ip-frag-offset, ip-ttl, ip-proto, ip-checksum, ip-saddr
  *
  * 1: ip-daddr
  *    udp: udp-sport, udp-dport, udp-len, udp-checksum, udp-payload (0:3, 4 bytes)
  *    icmp: icmp-type, icmp-code, icmp-checksum, icmp-payload (0:7, 8 bytes)
  *    tcp: tcp-sport, tcp-dport, tcp-seq-num, tcp-ack-num
  *
  * 2: udp: udp-payload (4:19, 16 bytes)
  *    icmp: icmp-payload (8:23, 16 bytes)
  *    tcp: tcp-offset, tcp-flags (all), tcp-win, tcp-checksum, tcp-urg-ptr,
  *    tcp-opt: 
  *
  * 3: udp: udp-payload (20:35, 16 bytes)
  *    icmp: icmp-payload (24:39, 16 bytes)
  *    tcp-opt: 
  *
  *
  * 4: udp: udp-payload (36:51, 16 bytes)
  *    icmp: icmp-payload (40:55, 16 bytes)
  *    tcp-opt: 
  *
  *
  */

  ip4_header_t *ip_mask = (ip4_header_t *) mask;
  ip4_header_t *ip_key = (ip4_header_t *) key;
 
  if (is_match) {
#ifdef MMB_MATCH_IP_VERSION
     ip_mask->ip_version_and_header_length = 0xf0;
     ip_key->ip_version_and_header_length = 0x40;
#elif
     if (vec_len(rule->matches) == 0) {
        ip_mask->ip_version_and_header_length = 0xf0;
        ip_key->ip_version_and_header_length = 0x40;  
     }
#endif
  }

  if (is_match) {
     mmb_match_t *match;
     vec_foreach(match, rule->matches) {
       mmb_ip4_mask_and_key_inline(ip_mask, ip_key, match->field, match->value);
     }
  } else {
     mmb_target_t *target;
     vec_foreach(target, rule->targets) {
       mmb_ip4_mask_and_key_inline(ip_mask, ip_key, target->field, target->value);
     }
  }

  ip4_header_host_to_net(ip_mask);
  ip4_header_host_to_net(ip_key);

   if (0);
#define _(a,b) else if (rule->l4 == IP_PROTOCOL_##b) {\
                 mmb_##a##_mask_and_key(rule, mask, key, 20, is_match);}
   foreach_mmb_transport_proto
#undef _
}

static_always_inline void mmb_ip6_mask_and_key_inline(ip6_header_t *ip_mask,
      ip6_header_t *ip_key, u8 field, u8 *value) {

   switch (field) {
      case MMB_FIELD_IP6_VER:
         ip_mask->ip_version_traffic_class_and_flow_label |= 0xf0000000;
         ip_key->ip_version_traffic_class_and_flow_label |= (*value << 28);
         break;
      case MMB_FIELD_IP6_TRAFFIC_CLASS:
         ip_mask->ip_version_traffic_class_and_flow_label |= 0x0ff00000;
         ip_key->ip_version_traffic_class_and_flow_label |= (*value << 20);
         break;
      case MMB_FIELD_IP6_FLOW_LABEL:
         ip_mask->ip_version_traffic_class_and_flow_label |= 0x0ffffff;
         ip_key->ip_version_traffic_class_and_flow_label |= bytes_to_u32(value);
         break;
      case MMB_FIELD_IP6_LEN:
         ip_mask->payload_length = 0xffff;
         ip_key->payload_length = bytes_to_u32(value);
         break;
      case MMB_FIELD_IP6_NEXT:
         ip_mask->protocol = 0xff;
         ip_key->protocol = *value;
         break;
      case MMB_FIELD_IP6_HOP_LIMIT:
         ip_mask->hop_limit = 0xff;
         ip_key->hop_limit = *value;
         break;
      case MMB_FIELD_IP6_SADDR:
         ip_mask->src_address.as_u64[0] = 0xffffffffffffffff << (64-value[16]);
         ip_mask->src_address.as_u64[1] = 0xffffffffffffffff << (128-value[16]);
         ip_key->src_address.as_u64[0] = bytes_to_u64(value) & ip_mask->src_address.as_u64[0];
         ip_key->src_address.as_u64[1] = bytes_to_u64(value+8) & ip_mask->src_address.as_u64[1];
         break;
      case MMB_FIELD_IP6_DADDR:
         ip_mask->dst_address.as_u64[0] = 0xffffffffffffffff << (64-value[16]);
         ip_mask->dst_address.as_u64[1] = 0xffffffffffffffff << (128-value[16]);
         ip_key->dst_address.as_u64[0] = bytes_to_u64(value) & ip_mask->dst_address.as_u64[0];
         ip_key->dst_address.as_u64[1] = bytes_to_u64(value+8) & ip_mask->dst_address.as_u64[1];
         break;
      case MMB_FIELD_IP6_PAYLOAD: 
         /* XXX */
         break;
      default:
         break;
    }

}

static void mmb_ip6_mask_and_key(mmb_rule_t *rule, u8 *mask, u8 *key, int is_match) {
   /* ip6
    * 0: ip6-ver, ip6-traffic-class, ip6-flow-label, ip6-len, ip6-next,
    *    ip6-hop-limit, ip6-saddr (8 bytes)
    *
    * 1: ip6-saddr (8 bytes), ip6-daddr (8 bytes)
    *
    * 2: ip6-daddr (8 bytes)
    *    udp: udp-sport, udp-dport, udp-len, udp-checksum
    *    icmp: icmp-type, icmp-code, icmp-checksum, icmp-payload (0:3, 4 bytes)
    *    tcp: tcp-sport, tcp-dport, tcp-seq-num, tcp-ack-num
    *    icmp: icmp-payload (8:23, 16 bytes)
    *    tcp: tcp-offset, tcp-flags (all), tcp-win, tcp-checksum, tcp-urg-ptr,
    *    tcp-opt: 
    *
    * 3: udp: udp-payload (20:35, 16 bytes)
    *    icmp: icmp-payload (24:39, 16 bytes)
    *    tcp-opt: 
    *
    *
    * 4: udp: udp-payload (36:51, 16 bytes)
    *    icmp: icmp-payload (40:55, 16 bytes)
    *    tcp-opt: 
    *
    *
    */


   ip6_header_t *ip_mask = (ip6_header_t *) mask;
   ip6_header_t *ip_key = (ip6_header_t *) key;

   if (is_match) {
#ifdef MMB_MATCH_IP_VERSION
     ip_mask->ip_version_traffic_class_and_flow_label = 0xf0000000;
     ip_key->ip_version_traffic_class_and_flow_label = 0x60000000;
#elif
     if (vec_len(rule->matches) == 0) {
       ip_mask->ip_version_traffic_class_and_flow_label = 0xf0000000;
       ip_key->ip_version_traffic_class_and_flow_label = 0x60000000;
     }
#endif
   }
 
  if (is_match) {
     mmb_match_t *match;
     vec_foreach(match, rule->matches) {
       mmb_ip6_mask_and_key_inline(ip_mask, ip_key, match->field, match->value);
     }
  } else {
     mmb_target_t *target;
     vec_foreach(target, rule->targets) {
       mmb_ip6_mask_and_key_inline(ip_mask, ip_key, target->field, target->value);
     }
  }

  ip6_header_host_to_net(ip_mask);
  ip6_header_host_to_net(ip_key);

   if (0);
#define _(a,b) else if (rule->l4 == IP_PROTOCOL_##b) {\
                 mmb_##a##_mask_and_key(rule, mask, key, 40, is_match);}
   foreach_mmb_transport_proto
#undef _
}

/**
 * Compute mask, key, skip and match from a rule.
 * 
 */
static void mmb_mask_and_key_aux(mmb_rule_t *rule, int is_match) {
  u32 skip = 0;
  u32 match = 0;
  u8 *mask = 0;
  u8 *key = 0;
  int i;

  vec_validate_aligned(mask, MMB_CLASSIFY_MAX_MASK_LEN-1, sizeof(u32x4));
  vec_validate_aligned(key, MMB_CLASSIFY_MAX_MASK_LEN-1, sizeof(u32x4));

   if (0);
#define _(a,b) else if (rule->l3 == ETHERNET_TYPE_##b) {\
                 mmb_##a##_mask_and_key(rule, mask, key, is_match);}
  foreach_mmb_network_proto
#undef _
  
  /* Scan forward looking for the first significant mask octet */
  for (i = 0; i < vec_len(mask); i++)
    if (mask[i])
      break;

  /* remove leading zeroes and compute skip */
  skip = i / sizeof(u32x4);
  vec_delete(mask, skip * sizeof(u32x4), 0);
  vec_delete(key, skip * sizeof(u32x4), 0);
  
  /* remove trailing zeroes and compute match */
  match = vec_len(mask) / sizeof(u32x4);
  for (i = match*sizeof(u32x4); i > 0; i-= sizeof(u32x4)) {
    u64 *tmp = (u64 *) (mask + (i-sizeof(u32x4)));
      if (*tmp || *(tmp+1))
        break;
    match--;
  }
  if (match == 0)
    clib_warning ("BUG: match 0");
  _vec_len(mask) = match * sizeof(u32x4);
  _vec_len(key) = match * sizeof(u32x4);
  
  if (is_match) {
     rule->classify_mask = mask;
     rule->classify_skip = skip;
     rule->classify_match = match;
     rule->classify_key = key;
  } else {
     /* flip mask */
     vec_foreach_index(i, mask) 
       mask[i] = ~mask[i];

     rule->rewrite_mask = mask;
     rule->rewrite_skip = skip;
     rule->rewrite_match = match;
     rule->rewrite_key = key;
  }
}

static_always_inline void mmb_mask_and_key(mmb_rule_t *rule) {
   mmb_mask_and_key_aux(rule, 1);
   /*u32 skip = rule->classify_skip;
   u32 match = rule->classify_match;
   u8 *key = rule->classify_key;
   u8 *mask = rule->classify_mask;
   int i;
   vl_print(mmb_main.vlib_main,"skip:%u match:%u lenmask:%u lenkey:%u",
                    skip,match,vec_len(mask),vec_len(key));
   vl_print(mmb_main.vlib_main,"MASK");
   for (i=0;i<vec_len(mask);i++) 
      vl_print(mmb_main.vlib_main,"%02X",mask[i]);
   vl_print(mmb_main.vlib_main,"KEY");
   for (i=0;i<vec_len(key);i++) 
      vl_print(mmb_main.vlib_main,"%02X",key[i]);*/
   if (!is_drop(rule)) { /* XXX tcp opts */
      mmb_mask_and_key_aux(rule, 0);
   }
}

static int
mmb_classify_add_table(u8 *mask, u32 skip, u32 match,
			              u32 *table_index, u32 next_table_index,
			               int max_entries)
{
  mmb_main_t *mm = &mmb_main;
  vnet_classify_main_t *cm = mm->classify_main;
  u32 nbuckets = max_entries;
  u32 memory_size = nbuckets++ << 14; /* ??? */
  u32 miss_next_index = IP_LOOKUP_NEXT_REWRITE;
  u32 current_data_flag = 0;
  int current_data_offset = 0;

  void *oldheap = clib_mem_set_heap (cm->vlib_main->heap_base);
  int ret = vnet_classify_add_del_table (cm, mask, nbuckets,
				      memory_size, skip, match,
				      next_table_index, miss_next_index,
				      table_index, current_data_flag,
				      current_data_offset, 1, 1);
  clib_mem_set_heap (oldheap);
  return ret;
}

static int
mmb_classify_del_table(u32 *table_index, int del_chain)
{
  mmb_main_t *mm = &mmb_main;
  vnet_classify_main_t *cm = mm->classify_main;

  void *oldheap = clib_mem_set_heap (cm->vlib_main->heap_base);
  int ret = vnet_classify_add_del_table (cm, 0, 0,
				      0, 0, 0,
				      0, 0,
				      table_index, 0,
				      0, 0, del_chain);
  clib_mem_set_heap (oldheap);
  return ret;
}

static int
mmb_classify_update_table (u32 *table_index, u32 next_table_index)
{
  mmb_main_t *mm = &mmb_main;
  vnet_classify_main_t *cm = mm->classify_main;

  void *oldheap = clib_mem_set_heap (cm->vlib_main->heap_base);
  int ret = vnet_classify_add_del_table (cm, NULL, 0, 0, 0, 0, 
                                         next_table_index, 0,
				                             table_index, 0, 0, 1, 1);
  clib_mem_set_heap (oldheap);
  return ret;
}

static int mmb_add_del_session(u32 table_index, u8 *key, u32 next_node, 
                               u32 rule_index, int is_add) {
  mmb_main_t *mm = &mmb_main;
  vnet_classify_main_t *cm = mm->classify_main;

  void *oldheap = clib_mem_set_heap (cm->vlib_main->heap_base);
  int ret = vnet_classify_add_del_session (mm->classify_main, 
                                table_index, key, 
                                next_node, 
                                rule_index, /* XXX */
                                0 /* advance */, 
                                0 /* action*/, 
                                0 /* metadata */,
                                is_add);
  clib_mem_set_heap (oldheap);
  return ret;
}

/*
 * return 1 if masks are equals
 */
static_always_inline u8 mask_equal(u8 *a, u8 *b) {
   if (vec_len(a) != vec_len(b))
      return 0;
   uword index;
   vec_foreach_index(index, a) {
      if (a[index] != b[index])
         return 0;
   }
   return 1;
}

/**
 * Attach first table to interfaces.
 *
 */
static void attach_table_intfc(u32 table_index, int is_add) {

  mmb_main_t *mm = &mmb_main;
  u32 sw_if_index;
  int i;

  for (i=0;i<vec_len(mm->sw_if_indexes);i++) {
     sw_if_index = mm->sw_if_indexes[i];
     vnet_set_mmb_classify_intfc(mm->vlib_main, sw_if_index,
                                 table_index, ~0, is_add);
     vl_print(mm->vlib_main, "table:%u add:%d if%u", table_index,
              is_add, sw_if_index);
  }
}

static_always_inline u32 find_table(u8* mask, u32 skip, u32 match) {
   /* return table index that for this rule, or ~0 if not found  */
   mmb_main_t *mm = &mmb_main;
   mmb_table_t *tables = mm->tables;
   mmb_table_t *table;
   u32 index;

   vec_foreach_index(index, tables) {
      table = &tables[index];
      if (mask_equal(table->mask, mask) && table->skip == skip
            && table->match == match)
         return index;
   }

   return ~0;
}

static_always_inline void add_table(u32 index, u8* mask, u32 skip, 
                                    u32 match, u32 previous_index,
                                    u32 entry_count, u32 size) {

  mmb_main_t *mm = &mmb_main;
  mmb_table_t table;

  memset(&table, 0, sizeof(mmb_table_t));
  table.index = index;
  table.mask = mask;
  table.skip = skip;
  table.match = match;
  table.previous_index = previous_index;
  table.next_index = ~0;
  table.entry_count = entry_count;
  table.size = size;

  vec_add1(mm->tables, table);
}

static void realloc_table(mmb_table_t *table) {
  /* Increase table size by a factor MMB_TABLE_SIZE_RATIO */

  mmb_main_t *mm = &mmb_main;
  mmb_table_t *tables = mm->tables;
  mmb_rule_t *rule, *rules = mm->rules;
  mmb_table_t *previous_table, *next_table;
  u32 old_index = table->index;
  u32 index;

  /* create bigger table */
  table->index = ~0;
  table->size *= MMB_TABLE_SIZE_RATIO;  
  mmb_classify_add_table(table->mask, table->skip, table->match,
			                &table->index, table->next_index, table->size);
  vl_print(mm->vlib_main, "new table of size %u created at index %u to replace index %u", 
               table->size, table->index, old_index);

  /* add sessions from old table */
  vec_foreach_index(index, rules) {
    rule = &rules[index];
    if (rule->classify_table_index == old_index) {
      mmb_add_del_session(table->index, rule->classify_key, 
                      next_if_match(rule), index, 1); 
      vl_print(mm->vlib_main, "added session %u to table %u", 
               index, table->index);
    }
  }  

  /* chain new table to previous */
  if (table->previous_index != ~0) {

      vec_foreach(previous_table, tables) {
         if (previous_table->index == table->previous_index)
           break;
      }
      vl_print(mm->vlib_main, "chaining table %u to previous table at index %u", 
               table->index, previous_table->index);

      mmb_classify_update_table (&previous_table->index, table->index);
      previous_table->next_index = table->index;

  } else /* if first table, update classifier */
     attach_table_intfc(table->index, 1);

  /* update next table field prev_index */
  if (table->next_index != ~0) {
      vec_foreach(next_table, tables) {
         if (next_table->index == table->next_index)
           break;
      } 
      vl_print(mm->vlib_main, "chaining table %u to next table at index %u", 
               table->index, next_table->index);    
      next_table->previous_index = table->index;
  }

  /* delete old sessions and table */
  vec_foreach(rule, rules) {
    if (rule->classify_table_index == old_index) {
      vl_print(mm->vlib_main, "deleting session from table %u", 
               old_index);
      mmb_add_del_session(rule->classify_table_index, rule->classify_key, 
                          0, index, 0); 
      rule->classify_table_index = table->index;
    }
  }  
  mmb_classify_del_table(&old_index, 0);
}

static u8 add_to_classifier(mmb_rule_t *rule) {
  /**
    * if table does not exists, create it and add session
    * if table exists, check size 
    *                  if size too small, enlarge
    *                  if size ok, add session, 
    */ 

  mmb_main_t *mm = &mmb_main;
  u32 rule_index = vec_len(mm->rules);
  u32 table_count = vec_len(mm->tables);
  int ret, next_node = next_if_match(rule);
  mmb_mask_and_key(rule);

  if (table_count == 0) {
      /* First rule, add table, session and chain table to if */
      mmb_classify_add_table(rule->classify_mask, 
         rule->classify_skip, rule->classify_match,
			&rule->classify_table_index, ~0, MMB_TABLE_SIZE_INIT);

      ret = mmb_add_del_session(rule->classify_table_index, rule->classify_key, 
                                next_node, rule_index, 1);

      add_table(rule->classify_table_index, rule->classify_mask, 
                rule->classify_skip, rule->classify_match, ~0,
                1, MMB_TABLE_SIZE_INIT);
      attach_table_intfc(rule->classify_table_index, 1);
  } else {

    u32 mmb_table = find_table(rule->classify_mask, 
                                    rule->classify_skip, 
                                    rule->classify_match);
    if (mmb_table == ~0) {
      /* Table does not exist, create it, add rule, and chain it to last table */
      mmb_classify_add_table(rule->classify_mask, 
         rule->classify_skip, rule->classify_match,
			&rule->classify_table_index, ~0, MMB_TABLE_SIZE_INIT);
      
      ret = mmb_add_del_session(rule->classify_table_index, rule->classify_key, 
                                next_node, rule_index, 1);

      mmb_table_t *last_table = &mm->tables[table_count-1];
      u32 last_table_index = last_table->index;	
      mmb_classify_update_table (&last_table_index, rule->classify_table_index);
      last_table->next_index = rule->classify_table_index;

      add_table(rule->classify_table_index, rule->classify_mask, 
                rule->classify_skip, rule->classify_match, last_table_index,
                1, MMB_TABLE_SIZE_INIT);

      vl_print(mm->vlib_main, "table:%u created and chained after table:%u", 
               rule->classify_table_index, last_table_index);
    } else { /* Found table */

      mmb_table_t *table = &mm->tables[mmb_table];
      if (table->entry_count == table->size)  /* Realloc table */
         realloc_table(table);   

      /* Table exists, add session */
      rule->classify_table_index = table->index;
      ret = mmb_add_del_session(rule->classify_table_index, rule->classify_key, 
                                next_node, rule_index, 1);
      vl_print(mm->vlib_main, "session added to table:%u rv:%d", 
               rule->classify_table_index, ret);
      if (ret) /* entry already in table (or some other error) */
        return 0;
      table->entry_count++;
    }
  }

  return 1;
}

static clib_error_t *
add_rule_command_fn (vlib_main_t * vm, unformat_input_t * input, 
                     vlib_cli_command_t * cmd) {
  unformat_input_tolower(input);

  mmb_rule_t rule;
  clib_error_t *error;
  mmb_main_t *mm = &mmb_main;

  init_rule(&rule);
  if ( (error = parse_rule(input, &rule)) )
    return error;
  
  vec_add1(mm->rules, rule);
  
  /* flags */
  if (rule_has_tcp_options(&rule))
     mm->opts_in_rules = 1;

   if (!mm->enabled) 
      mmb_enable_disable_all(1);
  
  vlib_cli_output(vm, "Added rule: %U", mmb_format_rule, &rule);
  return 0;
}

clib_error_t *parse_rule(unformat_input_t * input, 
                                      mmb_rule_t *rule) {
  if (unformat(input, "last")) 
    rule->last_match = 1;
  if (!unformat(input, "%U", mmb_unformat_rule, rule))
    return clib_error_return(0, "Invalid rule");
  
  clib_error_t *error;
  if ( (error = validate_rule(rule)) )
    return error;

  if (!add_to_classifier(rule))
    return clib_error_return(0, "Could not add rule to classifier");

  return 0;
}

static clib_error_t*
del_rule_command_fn(vlib_main_t *vm,
                    unformat_input_t *input,
                    vlib_cli_command_t *cmd) {
   unformat_input_tolower(input);
   u32 rule_index;
   mmb_main_t *mm = &mmb_main;
   mmb_rule_t *rules = mm->rules;

   if (unformat(input, "%u", &rule_index)) {

      if (rule_index > 0 && rule_index <= vec_len(rules)) {

         if (unformat_is_eof(input)) {
            rule_index--; 
            mmb_rule_t *rule = &rules[rule_index];
            free_rule(rule);
            vec_delete(rules, 1, rule_index);

            /* flags */
            update_flags(mm, rules);

            if (mm->enabled && vec_len(rules) == 0) 
               mmb_enable_disable_all(0);

            /* XXX: rebuild classifier: if table contains more than 1 session, del session,
                if table contains 1 session, del sess, del table, update table linked list
               
              */
            return 0;
         } 

         return clib_error_return(0, 
               "Syntax error: unexpected additional element");
      }

      return clib_error_return(0, "No rule at this index");
   }
 
   return clib_error_return(0, 
    "Syntax error: rule number must be an integer greater than 0");
}

static_always_inline void translate_ip4_ecn(u8 field, u8 **value) {
  switch (field) {
    case MMB_FIELD_IP4_NON_ECT:
      vec_add1(*value, 0);
      break;
    case MMB_FIELD_IP4_ECT0:
      vec_add1(*value, 2);
      break;
    case MMB_FIELD_IP4_ECT1:
      vec_add1(*value, 1);
      break;
    case MMB_FIELD_IP4_CE:
      vec_add1(*value, 3);
      break;
    default:
      break;
  }
}

static_always_inline void translate_match_ip4_ecn(mmb_match_t *match) {
  translate_ip4_ecn(match->field, &match->value);
  match->field = MMB_FIELD_IP4_ECN;
  match->condition = MMB_COND_EQ;
}

static_always_inline void translate_target_ip4_ecn(mmb_target_t *target) {
  translate_ip4_ecn(target->field, &target->value);
  target->field = MMB_FIELD_IP4_ECN;
}

static_always_inline void translate_match_bit_flags(mmb_match_t *match) {
  match->condition = MMB_COND_EQ;
  vec_add1(match->value, 1);
}

u16 get_field_protocol(u8 field) {
  if (MMB_FIELD_IP4_VER <= field && field <= MMB_FIELD_IP4_PAYLOAD)
     return ETHERNET_TYPE_IP4;
  if (MMB_FIELD_IP6_VER <= field && field <= MMB_FIELD_IP6_PAYLOAD)
     return ETHERNET_TYPE_IP6;
   else if (MMB_FIELD_ICMP_TYPE <= field && field <= MMB_FIELD_ICMP_PAYLOAD)
     return IP_PROTOCOL_ICMP;
   else if (MMB_FIELD_UDP_SPORT <= field && field <= MMB_FIELD_UDP_PAYLOAD)
     return IP_PROTOCOL_UDP;
   else if (MMB_FIELD_TCP_SPORT <= field && field <= MMB_FIELD_TCP_OPT)
     return IP_PROTOCOL_TCP;
   return IP_PROTOCOL_RESERVED;
}

u8 is_fixed_length(u8 field) {
   u8 index = field_toindex(field);
   if (index < fields_len)
      return fixed_len[index];
   return 0;
}

static_always_inline clib_error_t *update_l3(u8 field, u16 *derived_l3) {
 u16 proto = get_field_protocol(field);

 switch (proto) {
   case ETHERNET_TYPE_IP4:
   case ETHERNET_TYPE_IP6:

      if (*derived_l3 == 0) 
        *derived_l3 = proto;
      else if (*derived_l3 != proto)
        return clib_error_return(0, "Multiple l3 protocols");
   default:
     break;
 }
 return NULL;
}

static_always_inline clib_error_t *update_l4(u8 field, u8 *derived_l4) {
 u16 proto = get_field_protocol(field);

 switch (proto) {
   case IP_PROTOCOL_ICMP:case IP_PROTOCOL_UDP:
   case IP_PROTOCOL_TCP:

      if (*derived_l4 == IP_PROTOCOL_RESERVED) 
        *derived_l4 = proto;
      else if (*derived_l4 != proto)
        return clib_error_return(0, "Multiple l4 protocols");
   default:
     break;
 }
 return NULL;
}

static_always_inline clib_error_t*
validate_if(mmb_rule_t *rule, mmb_match_t *match, u8 field) {
   mmb_main_t mm = mmb_main;

   if (vec_len(match->value) == 0)
      return clib_error_return(0, "missing interface name/index"); 
   if (match->reverse || match->condition != MMB_COND_EQ)
      return clib_error_return(0, "invalid interface definition");

   u32 sw_if_index = bytes_to_u32(match->value); 
   if (vnet_get_sw_interface_safe (mm.vnet_main, sw_if_index) == NULL)
      return clib_error_return(0, "invalid interface index:%u", sw_if_index);

   if (field == MMB_FIELD_INTERFACE_IN && rule->in == ~0) 
      rule->in = sw_if_index;
   else if (field == MMB_FIELD_INTERFACE_OUT && rule->out == ~0)
      rule->out = sw_if_index;
   else
      return clib_error_return(0, "multiple interfaces");
   
   return NULL;
}

clib_error_t* validate_matches(mmb_rule_t *rule) {
   clib_error_t *error;
   uword index = 0;
   uword *deletions = 0, *deletion;

   vec_foreach_index(index, rule->matches) {
     mmb_match_t *match = &rule->matches[index];
     u8 field = match->field, reverse = match->reverse;
     u8 condition = match->condition;

     if ( (error = update_l3(field, &rule->l3))
           || (error = update_l4(field, &rule->l4)) )
       return error;

     switch (field) {
       case MMB_FIELD_ALL:
         /* other fields must be empty, and no other matches */
         if (condition || vec_len(match->value) > 0 || reverse
             || vec_len(rule->matches) > 1)
           return clib_error_return(0, "'all' in a <match> must be used alone");
         break;

       case MMB_FIELD_IP4_NON_ECT:case MMB_FIELD_IP4_ECT0:
       case MMB_FIELD_IP4_ECT1:case MMB_FIELD_IP4_CE:
         if (vec_len(match->value) > 0)
           return clib_error_return(0, "%s does not take a condition nor a value", 
                                    fields[field_toindex(field)]);
         translate_match_ip4_ecn(match);
         break;

       case MMB_FIELD_TCP_FLAGS:
         break; // TODO: translate to bits
       case MMB_FIELD_IP4_FLAGS:
         break; // TODO: translate to bits

       case MMB_FIELD_IP4_RES:case MMB_FIELD_IP4_DF:case MMB_FIELD_IP4_MF:
       case MMB_FIELD_TCP_CWR:case MMB_FIELD_TCP_ECE:case MMB_FIELD_TCP_URG:
       case MMB_FIELD_TCP_ACK:case MMB_FIELD_TCP_PUSH:case MMB_FIELD_TCP_RST:
       case MMB_FIELD_TCP_SYN:case MMB_FIELD_TCP_FIN:
         /* "bit-field" or "!bit-field" means "bit-field == 1" or "bit-field == 0" */
         /* so this does NOT mean "bit-field is (not) present in current packet" */
         if (vec_len(match->value) == 0)
           translate_match_bit_flags(match);
         break;
#define _(a,b,c) case a: {match->field=MMB_FIELD_TCP_OPT; match->opt_kind=c;\
                          rule->opts_in_matches=1; break;}
   foreach_mmb_tcp_opts
#undef _
       case MMB_FIELD_TCP_OPT:
         rule->opts_in_matches=1;
         break;
       case MMB_FIELD_INTERFACE_IN:  
       case MMB_FIELD_INTERFACE_OUT:
          if ( (error = validate_if(rule, match, field)) )
            return error;
          vec_insert_elt_last(deletions, &index);
          break;        
       default:
         break;
     }
   
     /* remove field if no value */
     if (vec_len(match->value) == 0 && match->field != MMB_FIELD_TCP_OPT)
       vec_insert_elt_last(deletions, &index);
   }

   /* delete interface fields */
   vec_foreach(deletion, deletions) {
     mmb_match_t *match = &rule->matches[*deletion];
     vec_free(match->value);
     if (vec_len(rule->matches) == 1) {
       match->field = MMB_FIELD_ALL;
       match->condition = 0;
     } else  /* del */  
       vec_delete(rule->matches, 1, *deletion);
   }

   return NULL;
}

static_always_inline mmb_transport_option_t to_transport_option(mmb_target_t *target) {
   mmb_transport_option_t opt;
   memset(&opt, 0, sizeof(mmb_transport_option_t));
   opt.l4 = IP_PROTOCOL_TCP;
   opt.kind = target->opt_kind;
   opt.value = target->value;
   return opt;
}

clib_error_t *validate_targets(mmb_rule_t *rule) {
   clib_error_t *error;
   uword index = 0;
   uword *rm_indexes = 0, *rm_index;

   vec_foreach_index(index, rule->targets) {
     mmb_target_t *target = &rule->targets[index];
     u8 field = target->field, reverse = target->reverse;
     u8 keyword = target->keyword, *value = target->value;

     if ( (error = update_l3(field, &rule->l3))
           || (error = update_l4(field, &rule->l4)) )
       return error;

     switch (field) {
       case MMB_FIELD_ALL:
         if (keyword != MMB_TARGET_STRIP || vec_len(value))
           return clib_error_return(0, "'all' in a <target> can only be used"
                                     " with the 'strip' keyword and no value");
         if (reverse)
           return clib_error_return(0, "<target> has no effect");
         
         break;
       case MMB_FIELD_INTERFACE_IN:
       case MMB_FIELD_INTERFACE_OUT:
          return clib_error_return(0, "invalid field in target");
       case MMB_FIELD_IP4_NON_ECT:case MMB_FIELD_IP4_ECT0:
       case MMB_FIELD_IP4_ECT1:case MMB_FIELD_IP4_CE:
         if (vec_len(value) > 0)
           return clib_error_return(0, "%s does not take a condition nor a value", 
                                    fields[field_toindex(field)]);
         translate_target_ip4_ecn(target);
         break;

#define _(a,b,c) case a: {target->field = MMB_FIELD_TCP_OPT; target->opt_kind=c;\
                          rule->opts_in_targets=1; break;}
   foreach_mmb_tcp_opts
#undef _
       //TODO: other "bit fields" (see above in "matches" part)
       case MMB_FIELD_TCP_OPT:
         rule->opts_in_targets=1;
         break;
       default:
         break;
     }
     
     if (keyword == MMB_TARGET_STRIP) {

       /* Ensure that field of strip target is a tcp opt. */
       if  (!(MMB_FIELD_TCP_OPT_MSS <= field 
             && field <= MMB_FIELD_ALL))
         return clib_error_return(0, "strip <field> must be a tcp option or 'all'");

       /* build option strip list  */
       if (!rule->has_strips)  {
         rule->has_strips = 1; 
         /* first strip target, set type */
         if (reverse) {
            rule->whitelist = 1;
            /* flip bitmap to 1s XXX: typo in func name !!*/
            clfib_bitmap_set_region(rule->opt_strips, 0, 1, 255);
         }
       } else if (rule->whitelist != reverse)
         return clib_error_return(0, "inconsistent use of ! in strip");

       if (field == MMB_FIELD_ALL) /* strip all */
         target->opt_kind = MMB_FIELD_TCP_OPT_ALL;

       clib_bitmap_set_no_check(rule->opt_strips, target->opt_kind, !rule->whitelist);
       vec_insert_elt_last(rm_indexes, &index);
     } else if (keyword == MMB_TARGET_ADD) { 

        /* Ensure that field of strip target is a tcp opt. */
       if  (!(MMB_FIELD_TCP_OPT_MSS <= field 
             && field < MMB_FIELD_ALL))
         return clib_error_return(0, "add <field> must be a tcp option");

       /* empty value should be fixed len and len = 0 */
       if (vec_len(value) == 0 
             && !(is_fixed_length(field) && lens[field_toindex(field)]==0) )
         return clib_error_return(0, "add <field> missing value");

       /* add transport opt to add-list and register for deletion */
       mmb_transport_option_t opt = to_transport_option(target);
       vec_add1(rule->opt_adds, opt);
       rule->has_adds = 1;
       vec_insert_elt_last(rm_indexes, &index);
     } else if (keyword == MMB_TARGET_MODIFY) { 
        if  (MMB_FIELD_TCP_OPT_MSS <= field 
             && field < MMB_FIELD_ALL) {
          vec_add1(rule->opt_mods, *target);
          vec_insert_elt_last(rm_indexes, &index);
        }
     }
   } 

   /* delete strips and adds from targets */ 
   vec_foreach(rm_index, rm_indexes) {
     vec_delete(rule->targets, 1, *rm_index);
   }
   
   return NULL;
}

clib_error_t *validate_rule(mmb_rule_t *rule) {
   clib_error_t *error; //TODO: more validation

   rule->l3 = 0;
   rule->l4 = IP_PROTOCOL_RESERVED;

   if ( (error = validate_matches(rule)) )
      return error;
   if ( (error = validate_targets(rule)) )
      return error; 

   if (rule->l3 == 0)
      rule->l3 = MMB_DEFAULT_ETHERNET_TYPE;

   return NULL;
}

void init_rule(mmb_rule_t *rule) {
  memset(rule, 0, sizeof(mmb_rule_t));
  rule->in = rule->out = ~0;
  rule->classify_table_index = ~0;
  clib_bitmap_alloc(rule->opt_strips, 255);
  clib_bitmap_zero(rule->opt_strips);
}

void free_rule(mmb_rule_t *rule) {
  uword index;

  vec_foreach_index(index, rule->matches) {
    vec_free(rule->matches[index].value);
  }
  vec_free(rule->matches);

  vec_foreach_index(index, rule->targets) {
    vec_free(rule->targets[index].value);
  }
  vec_free(rule->targets);

  clib_bitmap_free(rule->opt_strips);

  vec_foreach_index(index, rule->opt_adds) {
    vec_free(rule->opt_adds[index].value);
  }
  vec_free(rule->opt_adds);
}

/**
 * @brief CLI command to enable the mmb plugin.
 */
VLIB_CLI_COMMAND(sr_content_command_enable, static) = {
    .path = "mmb enable",
    .short_help = "mmb enable <interface-name> "
                  "(enable the MMB plugin on a given interface)",
    .function = enable_command_fn,
};

/**
 * @brief CLI command to disable the mmb plugin.
 */
VLIB_CLI_COMMAND(sr_content_command_disable, static) = {
    .path = "mmb disable",
    .short_help = "mmb disable <interface-name> "
                  "(disable the MMB plugin on a given interface)",
    .function = disable_command_fn,
};

/**
 * @brief CLI command to list all rules.
 */
VLIB_CLI_COMMAND(sr_content_command_list_rules, static) = {
    .path = "mmb list",
    .short_help = "Display all rules",
    .function = list_rules_command_fn,
};

/**
 * @brief CLI command to add a new rule.
 */
VLIB_CLI_COMMAND(sr_content_command_add_rule, static) = {
    .path = "mmb add",
    .short_help = "Add a rule: mmb add <field> [[<cond>] <value>] "
                  "[<field> [[<cond>] <value>] ...] <strip <option-field> "
                  "[strip|mod ...]|mod [<field>] <value> [strip|mod ...]|drop>",
    .function = add_rule_command_fn,
};

/**
 * @brief CLI command to insert a rule.
 */
VLIB_CLI_COMMAND(sr_content_command_insert_rule, static) = {
    .path = "mmb insert",
    .short_help = "Insert a rule: mmb insert [last] <index> <rule>",
    .function = insert_rule_command_fn,
};

/**
 * @brief CLI command to remove a rule.
 */
VLIB_CLI_COMMAND(sr_content_command_del_rules, static) = {
    .path = "mmb delete",
    .short_help = "Remove a rule: mmb delete <rule-number>",
    .function = del_rule_command_fn,
};

/**
 * @brief CLI command to remove all rules.
 */
VLIB_CLI_COMMAND(sr_content_command_flush_rule, static) = {
    .path = "mmb flush",
    .short_help = "Remove all rules",
    .function = flush_rules_command_fn,
};

/**
 * @brief Set up the API message handling tables.
 */
static clib_error_t *
mmb_plugin_api_hookup(vlib_main_t *vm) {
  CLIB_UNUSED(mmb_main_t) * mm = &mmb_main;
#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + mm->msg_id_base),     \
                           #n,					\
                           vl_api_##n##_t_handler,              \
                           vl_noop_handler,                     \
                           vl_api_##n##_t_endian,               \
                           vl_api_##n##_t_print,                \
                           sizeof(vl_api_##n##_t), 1); 
    foreach_mmb_plugin_api_msg;
#undef _

    return 0;
}

#define vl_msg_name_crc_list
#include <mmb/mmb_all_api_h.h>
#undef vl_msg_name_crc_list

static void 
setup_message_id_table(mmb_main_t * mm, api_main_t *am) {
#define _(id,n,crc) \
  vl_msg_api_add_msg_name_crc (am, #n "_" #crc, id + mm->msg_id_base);
  foreach_vl_msg_name_crc_mmb;
#undef _
}

/**
 * @brief Initialize the mmb plugin.
 */
static clib_error_t * mmb_init(vlib_main_t * vm) {
  mmb_main_t * mm = &mmb_main;
  clib_error_t * error = 0;
  u8 * name;

  memset(mm, 0, sizeof(mmb_main_t));
  mm->vnet_main = vnet_get_main();
  mm->vlib_main = vm;
  mm->classify_main = &vnet_classify_main;

  name = format (0, "mmb_%08x%c", api_version, 0);

  /* Ask for a correctly-sized block of API message decode slots */
  mm->msg_id_base = vl_msg_api_get_msg_ids 
      ((char *) name, VL_MSG_FIRST_AVAILABLE);

  error = mmb_plugin_api_hookup(vm);

  /* Add our API messages to the global name_crc hash table */
  setup_message_id_table(mm, &api_main);

  vec_free(name);

  return error;
}

VLIB_INIT_FUNCTION (mmb_init);

