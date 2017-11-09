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
#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/pg/pg.h>
#include <vppinfra/error.h>
#include <mmb/node.h>

typedef struct {
  u8  proto;
  u32 rule;
  u32 next;
} mmb_trace_t;

typedef enum {
  MMB_NEXT_LOOKUP,
  MMB_NEXT_DROP,
  MMB_N_NEXT
} mmb_next_t;

vlib_node_registration_t mmb_node;

#define foreach_mmb_error \
_(DONE, "MMB packets processed")

typedef enum {
#define _(sym,str) MMB_ERROR_##sym,
  foreach_mmb_error
#undef _
  MMB_N_ERROR,
} mmb_error_t;

static char * mmb_error_strings[] = {
#define _(sym,string) string,
  foreach_mmb_error
#undef _
};

/* packet trace format function */
static u8 * format_mmb_trace (u8 * s, va_list * args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  mmb_trace_t * t = va_arg (*args, mmb_trace_t *);

  if (t->rule == 0)
  {
    s = format(s, "MMB: no rule applied to packet IP with protocol %d (%s)\n",
                  t->proto,
                  (t->proto == IP_PROTOCOL_TCP) ? "TCP" : (t->proto == IP_PROTOCOL_UDP) ? "UDP" : (t->proto == IP_PROTOCOL_ICMP) ? "ICMP" : "OTHER");
  }
  else
  {
    s = format(s, "MMB: rule %u applied to packet IP with protocol %d (%s), action %s\n",
                   t->rule,
                   t->proto,
                   (t->proto == IP_PROTOCOL_TCP) ? "TCP" : (t->proto == IP_PROTOCOL_UDP) ? "UDP" : (t->proto == IP_PROTOCOL_ICMP) ? "ICMP" : "OTHER",
                   (t->next == MMB_NEXT_DROP) ? "DROP" : "FORWARD");
  }

  return s;
}

u8 value_compare(u64 a, u64 b, u8 condition, u8 reverse)
{
  u8 res;

  switch(condition)
  {
    case MMB_COND_EQ:
      res = (a == b);
      break;

    case MMB_COND_NEQ:
      res = (a != b);
      break;

    case MMB_COND_LEQ:
      res = (a <= b);
      break;

    case MMB_COND_GEQ:
      res = (a >= b);
      break;

    case MMB_COND_LT:
      res = (a < b);
      break;

    case MMB_COND_GT:
      res = (a > b);
      break;

    default:
      return 0;
  }

  if (reverse)
    return !res;

  return res;
}

u64 bytes_to_u64(u8 *bytes)
{
  u64 value = 0;
  u32 i = 0;
  u32 len = vec_len(bytes) - 1;

  vec_foreach_index(i, bytes)
    value += (bytes[i] << ((len - i) * 8));

  return value;
}

u8 packet_matches(ip4_header_t *ip, mmb_match_t *matches, u16 l3, u8 l4)
{
  /* Don't apply rules concerning layer 3 other than IP4 */
  if (l3 != ETHERNET_TYPE_IP4)
    return 0;

  /* Don't touch packets with layer 4 different than ICMP,UDP,TCP */
  if (ip->protocol != IP_PROTOCOL_ICMP 
      && ip->protocol != IP_PROTOCOL_UDP 
      && ip->protocol != IP_PROTOCOL_TCP)
  {
    return 0;
  }

  /* Don't apply rules with a layer 4 different than the packet's one */
  /* Note: case l4 = IP_PROTOCOL_RESERVED when "all" or only "ip-fields" in matching */
  if (l4 != IP_PROTOCOL_RESERVED && ip->protocol != l4)
    return 0;

  /* Rule could be applied to packet, let's check all matching conditions */
  uword imatch = 0;
  vec_foreach_index(imatch, matches)
  {
    mmb_match_t *match = &matches[imatch];

    /* "all" field -> MATCH */
    if (match->field == MMB_FIELD_ALL)
      return 1;

    /* get field related protocol */
    u16 field_protocol = get_field_protocol(match->field);

    /* First, search for "field" or "!field" match */
    switch(field_protocol)
    {
      case ETHERNET_TYPE_IP4:
      case IP_PROTOCOL_ICMP:
      case IP_PROTOCOL_UDP:
      case IP_PROTOCOL_TCP:
        if (match->condition == 0)
        {
          /* "!field" is always false for a related packet */
          if (match->reverse == 1)
            return 0;
        }
        break;

      default:
        break;
    }

    /* Then, check protocol condition */
    switch(field_protocol)
    {
      /* IP field */
      case ETHERNET_TYPE_IP4:
        if (!value_compare(GET_IP_FIELD(ip, match->field), bytes_to_u64(match->value), match->condition, match->reverse))
          return 0;
        break;

      /* ICMP field */
      case IP_PROTOCOL_ICMP:
        /* If not an ICMP packet, don't match */
        if (ip->protocol != field_protocol)
          return 0;

        icmp46_header_t *icmp = ip4_next_header(ip);
        if (!value_compare(GET_ICMP_FIELD(icmp, match->field), bytes_to_u64(match->value), match->condition, match->reverse))
          return 0;
        break;

      /* UDP field */
      case IP_PROTOCOL_UDP:
        /* If not an UDP packet, don't match */
        if (ip->protocol != field_protocol)
          return 0;

        udp_header_t *udp = ip4_next_header(ip);
        if (!value_compare(GET_UDP_FIELD(udp, match->field), bytes_to_u64(match->value), match->condition, match->reverse))
          return 0;
        break;

      /* TCP field */
      case IP_PROTOCOL_TCP:
        /* If not a TCP packet, don't match */
        if (ip->protocol != field_protocol)
          return 0;

        tcp_header_t *tcp = ip4_next_header(ip);
        if (!value_compare(GET_TCP_FIELD(tcp, match->field), bytes_to_u64(match->value), match->condition, match->reverse))
          return 0;
        break;

      /* Unexpected field */
      default:
        return 0;
    }
  }

  /* MATCH: none of the conditions triggered "false" */
  return 1;
}

u32 packet_apply_targets(vlib_main_t *vm, vlib_buffer_t *buffer, ip4_header_t *ip, mmb_target_t *targets)
{
  uword itarget = 0;
  u8 l4_recompute_checksum = 0;

  vec_foreach_index(itarget, targets)
  {
    mmb_target_t *target = &targets[itarget];

    if (target->keyword == MMB_TARGET_DROP)
      return MMB_NEXT_DROP;

    //TODO: waiting for the whitelist/blacklist in rules
    /*if (target->keyword == MMB_TARGET_STRIP)
    {
      //TODO (+ recompute checksum at the end)
      l4_recompute_checksum = 1;
      continue;
    }*/

    if (target->keyword == MMB_TARGET_MODIFY)
    {
      u16 field_protocol = get_field_protocol(target->field);

      switch(field_protocol)
      {
        case ETHERNET_TYPE_IP4:
          SET_IP_FIELD(ip, target->field, bytes_to_u64(target->value));
          break;

        case IP_PROTOCOL_ICMP:
          ;
          icmp46_header_t *icmp = ip4_next_header(ip);
          SET_ICMP_FIELD(icmp, target->field, bytes_to_u64(target->value));
          l4_recompute_checksum = 1;
          break;

        case IP_PROTOCOL_UDP:
          ;
          udp_header_t *udp = ip4_next_header(ip);
          SET_UDP_FIELD(udp, target->field, bytes_to_u64(target->value));
          l4_recompute_checksum = 1;
          break;

        case IP_PROTOCOL_TCP:
          ;
          tcp_header_t *tcp = ip4_next_header(ip);
          SET_TCP_FIELD(tcp, target->field, bytes_to_u64(target->value));
          l4_recompute_checksum = 1;
          break;

        default:
          break;
      }
    }
  }

  /* Re-compute L4 checksum if modified */
  if (l4_recompute_checksum)
  {
    switch(ip->protocol)
    {
      case IP_PROTOCOL_ICMP:
        ;
        icmp46_header_t *icmp = ip4_next_header(ip);
        icmp->checksum = 0;
        ip_csum_t csum = ip_incremental_checksum(0, icmp, clib_net_to_host_u16(ip->length) - sizeof(*ip));
        icmp->checksum = ~ip_csum_fold(csum);
        break;

      case IP_PROTOCOL_UDP:
        ;
        udp_header_t *udp = ip4_next_header(ip);
        udp->checksum = ip4_tcp_udp_compute_checksum(vm, buffer, ip);
        /* RFC 7011 section 10.3.2 */
        if (udp->checksum == 0)
          udp->checksum = 0xffff;
        break;

      case IP_PROTOCOL_TCP:
        ;
        tcp_header_t *tcp = ip4_next_header(ip);
        tcp->checksum = ip4_tcp_udp_compute_checksum(vm, buffer, ip);
        break;

      default:
        break;
    }
  }

  /* Re-compute L3 (IPv4) checksum */
  ip->checksum = ip4_header_checksum(ip);
  return MMB_NEXT_LOOKUP;
}

static uword
mmb_node_fn (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * frame)
{
  mmb_main_t *mm = &mmb_main;
  mmb_rule_t *rules = mm->rules;

  u32 n_left_from, * from, * to_next;
  mmb_next_t next_index;
  u32 pkts_done = 0;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  while (n_left_from > 0)
  {
    u32 n_left_to_next;

    vlib_get_next_frame (vm, node, next_index,
			 to_next, n_left_to_next);

    while (n_left_from >= 4 && n_left_to_next >= 2)
    {
      u32 next0 = MMB_NEXT_LOOKUP;
      u32 next1 = MMB_NEXT_LOOKUP;
      ip4_header_t *ip0, *ip1;
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
          
      /* Prefetch next iteration. */
      {
        vlib_buffer_t * p2, * p3;
            
        p2 = vlib_get_buffer (vm, from[2]);
        p3 = vlib_get_buffer (vm, from[3]);
            
        vlib_prefetch_buffer_header (p2, LOAD);
        vlib_prefetch_buffer_header (p3, LOAD);

        CLIB_PREFETCH (p2->data, CLIB_CACHE_LINE_BYTES, STORE);
        CLIB_PREFETCH (p3->data, CLIB_CACHE_LINE_BYTES, STORE);
      }

      /* speculatively enqueue b0 and b1 to the current next frame */
      to_next[0] = bi0 = from[0];
      to_next[1] = bi1 = from[1];
      from += 2;
      to_next += 2;
      n_left_from -= 2;
      n_left_to_next -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);
      
      ip0 = vlib_buffer_get_current (b0);
      ip1 = vlib_buffer_get_current (b1);

      pkts_done += 2;

      if (PREDICT_FALSE((node->flags & VLIB_NODE_FLAG_TRACE)))
      {
        if (b0->flags & VLIB_BUFFER_IS_TRACED) 
        {
          mmb_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
          t->proto = ip0->protocol;
          t->next = next0;
        }

        if (b1->flags & VLIB_BUFFER_IS_TRACED) 
        {
          mmb_trace_t *t = vlib_add_trace (vm, node, b1, sizeof (*t));
          t->proto = ip1->protocol;
          t->next = next1;
        }
      }
            
      /* verify speculative enqueues, maybe switch current next frame */
      vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
                                       to_next, n_left_to_next,
                                       bi0, bi1, next0, next1);
    }

    while (n_left_from > 0 && n_left_to_next > 0)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      u32 next0 = MMB_NEXT_LOOKUP;
      ip4_header_t *ip0;

      /* speculatively enqueue b0 to the current next frame */
      bi0 = from[0];
      to_next[0] = bi0;
      from += 1;
      to_next += 1;
      n_left_from -= 1;
      n_left_to_next -= 1;

      /* get vlib buffer */
      b0 = vlib_get_buffer (vm, bi0);
      
      /* get IP header */
      ip0 = vlib_buffer_get_current (b0);

      /* fetch each rule to find a match */
      uword irule = 0;
      u32 applied_rule_index = ~0;
      vec_foreach_index(irule, rules)
      {
        mmb_rule_t *rule = &rules[irule];

        if (packet_matches(ip0, rule->matches, rule->l3, rule->l4))
        {
          /* MATCH: apply targets to packet */
          next0 = packet_apply_targets(vm, b0, ip0, rule->targets);
          applied_rule_index = irule;
          break;
        }
      }

      /* one more packet processed */
      pkts_done += 1;

      /* node trace (if enabled) */
      if (PREDICT_FALSE((node->flags & VLIB_NODE_FLAG_TRACE) 
                        && (b0->flags & VLIB_BUFFER_IS_TRACED)))
      {
        mmb_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
        t->proto = ip0->protocol;
        t->rule = (applied_rule_index == ~0) ? 0 : applied_rule_index+1;
        t->next = next0;
      }

      /* verify speculative enqueue, maybe switch current next frame */
      vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
				       to_next, n_left_to_next,
				       bi0, next0);
    }

    vlib_put_next_frame (vm, node, next_index, n_left_to_next);
  }

  vlib_node_increment_counter (vm, mmb_node.index, 
                               MMB_ERROR_DONE, pkts_done);
  return frame->n_vectors;
}

VLIB_REGISTER_NODE (mmb_node) = {
  .function = mmb_node_fn,
  .name = "mmb",
  .vector_size = sizeof (u32),
  .format_trace = format_mmb_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  
  .n_errors = ARRAY_LEN(mmb_error_strings),
  .error_strings = mmb_error_strings,

  .n_next_nodes = MMB_N_NEXT,

  /* edit / add dispositions here */
  //TODO: we may need to change next nodes defined below
  .next_nodes = {
        [MMB_NEXT_LOOKUP] = "ip4-lookup",
        [MMB_NEXT_DROP]   = "error-drop", //ip4-drop
  },
};

VLIB_NODE_FUNCTION_MULTIARCH(mmb_node, mmb_node_fn);

