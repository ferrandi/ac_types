/**************************************************************************
 *                                                                        *
 *  Algorithmic C (tm) Datatypes                                          *
 *                                                                        *
 *  Copyright 2004-2020, Mentor Graphics Corporation,                     *
 *                                                                        *
 *  All Rights Reserved.                                                  *
 *                                                                        *
 **************************************************************************
 *  Licensed under the Apache License, Version 2.0 (the "License");       *
 *  you may not use this file except in compliance with the License.      *
 *  You may obtain a copy of the License at                               *
 *                                                                        *
 *      http://www.apache.org/licenses/LICENSE-2.0                        *
 *                                                                        *
 *  Unless required by applicable law or agreed to in writing, software   *
 *  distributed under the License is distributed on an "AS IS" BASIS,     *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or       *
 *  implied.                                                              *
 *  See the License for the specific language governing permissions and   *
 *  limitations under the License.                                        *
 **************************************************************************
 *                                                                        *
 *  This file was modified by the PandA team from Politecnico di Milano   *
 *  to generate efficient hardware for the PandA/Bambu HLS tool. This     *
 *  derivative distribution is licensed under the Apache License, Version  *
 *  2.0, with the BAMBU exceptions stated in the repository LICENSE file. *
 *                                                                        *
 *************************************************************************/

/*
//  Source:         ac_channel_sim.h
//  Description:    host-side definitions of the ac_channel seam
*/

// ac_channel.h declares the seam and never defines it: during synthesis the prototypes survive into the
// IR as undefined externals, which is exactly what bambu matches and replaces with FIFO ports. This file
// supplies the bodies for anything that actually runs on the host - the gold reference, the MDPI wrapper,
// the generated testbench - and those bodies work on the channel's own deque.
//
// It is injected with -include during the MDPI compilation (see simulation_wrapper.sh), so user sources
// we cannot edit get the definitions too. That also means it is fed to C translation units, hence the
// __cplusplus guard: outside C++ this file is empty on purpose, not an error.
//
// There are no explicit instantiations here and none are needed. The seam entries are member templates,
// so every host translation unit instantiates exactly the ones it calls, with vague linkage the linker
// folds - including for element types this header has never seen.

#ifndef __AC_CHANNEL_SIM_H
#define __AC_CHANNEL_SIM_H

#ifdef __cplusplus

#include "ac_channel.h"

#include <cstring>
#include <iostream>

#if !defined(AC_USER_DEFINED_ASSERT) && !defined(AC_ASSERT_THROW_EXCEPTION)
#include <cassert>
#endif

namespace ac_channel_detail
{
   // Moved here from ac_channel.h together with the software implementation it guards, so that
   // AC_USER_DEFINED_ASSERT and AC_ASSERT_THROW_EXCEPTION keep working as they did.
   inline void ac_assert(bool condition, const char* file, int line, const ac_channel_exception::code& code)
   {
#ifndef AC_USER_DEFINED_ASSERT
      if(!condition)
      {
         const ac_exception e(file, line, code, ac_channel_exception::msg(code));
#ifdef AC_ASSERT_THROW_EXCEPTION
#ifdef AC_ASSERT_THROW_EXCEPTION_AS_CONST_CHAR
         throw(e.msg);
#else
         throw(e);
#endif
#else
         std::cerr << "Assert";
         if(e.file)
         {
            std::cerr << " in file " << e.file << ":" << e.line;
         }
         std::cerr << " " << e.msg << std::endl;
         assert(0);
#endif
      }
#else
      AC_USER_DEFINED_ASSERT(condition, file, line, ac_channel_exception::msg(code));
#endif
   }
} // namespace ac_channel_detail

/////////////////////////////////////////////////
// Blocking read / peek
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_read_bambu_internal()
{
   ac_channel_detail::ac_assert(!ch.empty(), __FILE__, __LINE__, ac_channel_exception::read_from_empty_channel);
   const T0 v = _to_payload<T0>(ch.front());
   ch.pop_front();
   return v;
}

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_peek_bambu_internal()
{
   ac_channel_detail::ac_assert(!ch.empty(), __FILE__, __LINE__, ac_channel_exception::read_from_empty_channel);
   return _to_payload<T0>(ch.front());
}

/////////////////////////////////////////////////
// Non-blocking read / peek, packed form
//
// The payload is one bit wider than the data: bits [0, N) carry the value and bit N the valid flag,
// N being the logical width of T. The bool& is passed by the caller but ignored by it, so it is only
// mirrored here.
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_read_bambu_internal(bool& res)
{
   res = !ch.empty();
   if(!res)
   {
      return static_cast<T0>(0);
   }
   const T0 v = _to_payload<T0>(ch.front());
   ch.pop_front();
   return static_cast<T0>(v | (static_cast<T0>(1) << static_cast<T0>(ac_channel_packed<T>::bits)));
}

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_peek_bambu_internal(bool& res)
{
   res = !ch.empty();
   if(!res)
   {
      return static_cast<T0>(0);
   }
   const T0 v = _to_payload<T0>(ch.front());
   return static_cast<T0>(v | (static_cast<T0>(1) << static_cast<T0>(ac_channel_packed<T>::bits)));
}

/////////////////////////////////////////////////
// Non-blocking read / peek, flag-by-reference form (no _BitInt available)
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_read_bambu_internal(bool& res, bool& dummy)
{
   dummy = false;
   res = !ch.empty();
   if(!res)
   {
      T0 zero;
      std::memset(&zero, 0, sizeof(T0));
      return zero;
   }
   const T0 v = _to_payload<T0>(ch.front());
   ch.pop_front();
   return v;
}

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_peek_bambu_internal(bool& res, bool& dummy)
{
   dummy = false;
   res = !ch.empty();
   if(!res)
   {
      T0 zero;
      std::memset(&zero, 0, sizeof(T0));
      return zero;
   }
   return _to_payload<T0>(ch.front());
}

/////////////////////////////////////////////////
// Write
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) bool ac_channel<T>::_write_bambu_internal(T0 t)
{
   T v;
   _from_payload(v, t);
   ch.push_back(v);
   return true;
}

/////////////////////////////////////////////////
// Instantiation point, for a kernel that arrives already compiled
/////////////////////////////////////////////////

// A host translation unit only emits the seam entries it calls. In the normal flow that is exactly
// right: the gold is the user source recompiled, so it orders precisely what the design performs.
// When the kernel arrives as a .ll or an object instead, nothing on the host mentions the operations
// only the kernel does - a kernel calling nb_peek leaves _peek_bambu_internal undefined, and since it
// is weak the linker resolves it to zero and the gold jumps there. Naming them all once per element
// type fixes it:
//
//     template struct ac_channel_instantiate<ap_uint<16>>;
//
// anchor() is never meant to be called, only instantiated, so the reads it performs on an empty
// channel never happen.
template <class T>
struct ac_channel_instantiate
{
   static void anchor(ac_channel<T>& c)
   {
      T t = T();
      bool ok;
      c.write(t);
      ok = c.nb_write(t);
      t = c.read();
      c.read(t);
      ok = c.nb_read(t);
      t = c.peek();
      c.peek(t);
      ok = c.nb_peek(t);
      (void)ok;
   }
};

#endif // __cplusplus

#endif // __AC_CHANNEL_SIM_H
