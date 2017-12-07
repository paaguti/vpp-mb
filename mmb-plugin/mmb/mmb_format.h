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
#ifndef __included_mmb_format_h__
#define __included_mmb_format_h__

#include <vlibapi/api.h>

#include <mmb/mmb.h>

void unformat_input_tolower(unformat_input_t *input);

uword mmb_unformat_match(unformat_input_t * input, va_list *args);
uword mmb_unformat_target(unformat_input_t * input, va_list *args);

u8* mmb_format_rule(u8 *s, va_list *args); 
u8* mmb_format_rules(u8 *s, va_list *args);

#endif /* included_mmb_format_h */

