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
 * @brief MMB Plugin, format / unformat
 * @author K.Edeline
 */

#include <vlib/vlib.h>
#include <ctype.h>

#include <mmb/mmb_format.h>

static uword mmb_unformat_field(unformat_input_t *input, va_list *args);
static uword mmb_unformat_condition(unformat_input_t *input, va_list *args);
static uword mmb_unformat_value(unformat_input_t *input, va_list *args);
static uword mmb_unformat_ip4_address (unformat_input_t *input, va_list *args);
static u8 *mmb_format_match(u8 *s, va_list *args);
static u8 *mmb_format_target(u8 *s, va_list *args);
static u8* mmb_format_field(u8 *s, va_list *args);
static u8* mmb_format_condition(u8 *s, va_list *args);
static u8* mmb_format_keyword(u8 *s, va_list *args);
static u8 *mmb_format_rule_column(u8 *s, va_list *args);

static const char* blanks = "                                                "
                            "                                                "
                            "                                                "
                            "                                                ";

static_always_inline u8 *str_tolower(u8 *str) {
  for(int i = 0; str[i]; i++){
    str[i] = tolower(str[i]);
  }
  return str;
}

void unformat_input_tolower(unformat_input_t *input) {
  str_tolower(input->buffer);
}

static_always_inline void resize_value(u8 field, u8 **value) {
  u8 user_len = vec_len(*value);
  if (user_len == 0) return;

  u8 proper_len = lens[field_toindex(field)];
  if (proper_len == 0) return;

  if (user_len > proper_len)
    vec_delete(*value, user_len-proper_len, 0);
  else if (user_len < proper_len)
    vec_insert(*value, proper_len-user_len, 0);
}

uword mmb_unformat_field(unformat_input_t * input, va_list *args) {
  u8 *field = va_arg(*args, u8*);
  u8 *kind  = va_arg(*args, u8*);
  for (u8 i=0; i<fields_len; i++) {
    if (unformat (input, fields[i])) {
      *field = field_tomacro(i);
 
      /* optional kind */
      if (*field == MMB_FIELD_TCP_OPT
          && (unformat(input, "x%x", kind)
              || unformat(input, "0x%x", kind) 
              || unformat(input, "%d", kind)))
        ;
      return 1;
    }
  }

  return 0;
}

uword mmb_unformat_condition(unformat_input_t * input, va_list *args) {
  u8 *cond = va_arg(*args, u8*);
  for (u8 i=0; i<conditions_len; i++) {
    if (unformat (input, conditions[i])) {
      *cond = cond_tomacro(i);
      return 1;
    }
  }

  return 0;
}

static_always_inline void u64_tobytes(u8 **bytes, u64 value, u8 count) {
  for (int i=count-1; i>=0; i--)
    vec_add1(*bytes, value>>(i*8)); 
}

/* Parse an IP4 address %d.%d.%d.%d[/%d] */
uword mmb_unformat_ip4_address (unformat_input_t * input, va_list *args) {
  u8 **result = va_arg (*args, u8 **);
  unsigned a[5];

  if (unformat (input, "%d.%d.%d.%d/%d", &a[0], &a[1], &a[2], &a[3], &a[4])) {
    if (a[4] > 32)
      return 0;
  } else if (unformat (input, "%d.%d.%d.%d", &a[0], &a[1], &a[2], &a[3])) 
    a[4] = 32;
  else return 0;

  if (a[0] >= 256 || a[1] >= 256 || a[2] >= 256 || a[3] >= 256)
    return 0;

  vec_add1 (*result, a[0]);
  vec_add1 (*result, a[1]);
  vec_add1 (*result, a[2]);
  vec_add1 (*result, a[3]);
  vec_add1 (*result, a[4]);

  return 1;
}

uword mmb_unformat_value(unformat_input_t * input, va_list *args) {
  u8 **bytes = va_arg(*args, u8**);
  u8 found_l4 = 0;
  u16 found_l3 = 0;
  u64 decimal = 0;

   if (0);
#define _(a,b) else if (unformat (input, #a)) found_l4 = IP_PROTOCOL_##b;
   foreach_mmb_transport_proto
#undef _
#define _(a,b) else if (unformat (input, #a)) found_l3 = ETHERNET_TYPE_##b;
   foreach_mmb_network_proto
#undef _

  if (found_l4) {
    u64_tobytes(bytes, found_l4, 1);
    return 1;
  } else if (found_l3) {
    u64_tobytes(bytes, found_l3, 2);
    return 1;
  }

  if (unformat(input, "%U", mmb_unformat_ip4_address, bytes))   
    ;
  else if (unformat (input, "0x") 
    || unformat (input, "x")) {
    /* hex value */ 

    u8 *hex_str = 0;
    if (unformat (input, "%U", unformat_hex_string, bytes))
      ;
    else if (unformat (input, "%s", &hex_str)) {

      /* add an extra 0 for parity */  
      unformat_input_t str_input, *sub_input = &str_input; 
      unformat_init(sub_input, 0, 0);
      unformat_init_vector(sub_input, format(0, "0%s", hex_str));
      if (!unformat (sub_input, "%U", unformat_hex_string, bytes)) {
        unformat_free(sub_input);
        return 0;
      }
      unformat_free(sub_input);
    }
    
  } else if (unformat (input, "%lu", &decimal)) {
     u64_tobytes(bytes, decimal, 8);
  } else 
    return 0;

  return 1;
}

uword mmb_unformat_target(unformat_input_t * input, va_list *args) {
   mmb_target_t *target = va_arg(*args, mmb_target_t*);

   if (unformat(input, "strip ! %U", mmb_unformat_field, 
               &target->field, &target->opt_kind)) {
     target->keyword=MMB_TARGET_STRIP;
     target->reverse=1;
   } else if (unformat(input, "strip %U", mmb_unformat_field, 
                      &target->field, &target->opt_kind)) 
     target->keyword=MMB_TARGET_STRIP;
   else if (unformat(input, "mod %U %U", mmb_unformat_field, 
                    &target->field, &target->opt_kind, 
                    mmb_unformat_value, &target->value))
     target->keyword=MMB_TARGET_MODIFY; 
   else if (unformat(input, "drop"))
     target->keyword=MMB_TARGET_DROP; 
   else 
     return 0;
   
   resize_value(target->field, &target->value);
   return 1;
}

uword mmb_unformat_match(unformat_input_t * input, va_list *args) {
   mmb_match_t *match = va_arg(*args, mmb_match_t*);

   if (unformat(input, "!"))
     match->reverse = 1;
   
   if (unformat(input, "%U %U %U", mmb_unformat_field, 
                    &match->field, &match->opt_kind, mmb_unformat_condition, 
                    &match->condition, mmb_unformat_value, &match->value)) 
     ;
   else if (unformat(input, "%U %U", mmb_unformat_field, 
                    &match->field, &match->opt_kind, 
                    mmb_unformat_value, &match->value)) 
     match->condition = MMB_COND_EQ;
   else if (unformat(input, "%U", mmb_unformat_field,
                    &match->field, &match->opt_kind)) 
     ;
   else 
     return 0;

   resize_value(match->field, &match->value);
   return 1;
}

u8* mmb_format_field(u8* s, va_list *args) {
   u8 field = *va_arg(*args, u8*); 
   u8 kind  = *va_arg(*args, u8*);

   u8 field_index = field_toindex(field);
   if (field < MMB_FIELD_NET_PROTO 
    || field > MMB_FIELD_NET_PROTO+fields_len)
     ; 
   else if (field == MMB_FIELD_TCP_OPT && kind) {
     if (0);
#define _(a,b,c) else if (kind == c) s = format(s, "%s %s", fields[field_index], #b);
   foreach_mmb_tcp_opts
#undef _
     else s = format(s, "%s %d", fields[field_index], kind);
   } else
     s = format(s, "%s", fields[field_index]);

   return s;
}

u8* mmb_format_condition(u8* s, va_list *args) {
  u8 condition = *va_arg(*args, u8*);
  if (condition >= MMB_COND_EQ 
    &&  condition <= MMB_COND_EQ+conditions_len)
    s = format(s, "%s", conditions[cond_toindex(condition)]);
  
  return s;
}

u8* mmb_format_keyword(u8* s, va_list *args) {
  u8 keyword = *va_arg(*args, u8*);
   
  char *keyword_str = "";
  switch(keyword) {
    case MMB_TARGET_DROP:
       keyword_str = "drop";
       break;
    case MMB_TARGET_MODIFY:
       keyword_str =  "mod";
       break;
    case MMB_TARGET_STRIP:
       keyword_str =  "strip";
       break;
    default:
       break;
  }

  s = format(s, "%s", keyword_str);
  return s;
}

static_always_inline u8 *mmb_format_ip_protocol (u8 * s, va_list *args)
{
  u8 protocol = va_arg (*args, ip_protocol_t);
  if (protocol == IP_PROTOCOL_RESERVED)
    return format(s, "all");
  return format(s, "%U", format_ip_protocol, protocol);
}

u8 *mmb_format_rule(u8 *s, va_list *args) {
  mmb_rule_t *rule = va_arg(*args, mmb_rule_t*);
  s = format(s, "%-4U\t%-8U", format_ethernet_type, rule->l3, 
                              mmb_format_ip_protocol, rule->l4); 
  
  uword index = 0;
  vec_foreach_index(index, rule->matches) {
    s = format(s, "%U%s", mmb_format_match, &rule->matches[index],
                        (index != vec_len(rule->matches)-1) ? " AND ":"\t");
  }

  vec_foreach_index(index, rule->targets) {
    s = format(s, "%U%s", mmb_format_target, &rule->targets[index],  
                        (index != vec_len(rule->targets)-1) ? ", ":"");
  }
  return s;
}

static u8 *mmb_format_rule_column(u8 *s, va_list *args) {
  mmb_rule_t *rule = va_arg(*args, mmb_rule_t*);

  s = format(s, "%-4U  %-8U",
                format_ethernet_type, rule->l3, 
                mmb_format_ip_protocol, rule->l4); 
  
  for (uword index = 0; 
       index<clib_max(vec_len(rule->matches), vec_len(rule->targets)); 
       index++) {
    if (index < vec_len(rule->matches)) {
      /* tabulate empty line */
      if (index) 
        s = format(s, "%22s", "AND ");
      s = format(s, "%-40U", mmb_format_match, &rule->matches[index]);

    } else  
      s = format(s, "%62s", blanks);

    if (index < vec_len(rule->targets)) 
      s = format(s, "%-40U", mmb_format_target, &rule->targets[index]);
    s = format(s, "\n");

  }
  return s;
}

static_always_inline u8 *mmb_format_bytes(u8 *s, va_list *args) {
  u8 *byte, *bytes = va_arg(*args, u8*);
  u8 field = va_arg(*args, u32);
  switch (field) {
    case MMB_FIELD_IP_SADDR:
    case MMB_FIELD_IP_DADDR:
      s = format(s, "%U", format_ip4_address_and_length, bytes, bytes[4]);
      break;

    
    default:
      vec_foreach(byte, bytes) {
        s = format(s, "%02x", *byte);
      }
    break;
  }
  return s;
}

u8 *mmb_format_match(u8 *s, va_list *args) {

  mmb_match_t *match = va_arg(*args, mmb_match_t*);
  s = format(s, "%s%U %U %U", (match->reverse) ? "! ":"",
                          mmb_format_field, &match->field, &match->opt_kind,
                          mmb_format_condition, &match->condition,
                          mmb_format_bytes, match->value, match->field
                          );
  return s;
} 

u8 *mmb_format_target(u8 *s, va_list *args) {

  mmb_target_t *target = va_arg(*args, mmb_target_t*);
  s = format(s, "%s%U %U %U", (target->reverse) ? "! ":"",
                         mmb_format_keyword, &target->keyword,
                         mmb_format_field, &target->field, &target->opt_kind,
                         mmb_format_bytes, target->value, target->field
                         );
  return s; 
}

u8 *mmb_format_rules(u8 *s, va_list *args) {
  mmb_rule_t *rules = va_arg(*args, mmb_rule_t*);

  s = format(s, " Index%2sL3%4sL4%6sMatches%33sTargets\n", 
                blanks, blanks, blanks, blanks);
  uword rule_index = 0;
  vec_foreach_index(rule_index, rules) {
    s = format(s, " %d\t%U%s", rule_index+1, mmb_format_rule_column, 
               &rules[rule_index], rule_index == vec_len(rules)-1 ? "" : "\n");
  }
  return s;
}
