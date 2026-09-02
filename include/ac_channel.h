/**************************************************************************
 *                                                                        *
 *  Algorithmic C (tm) Datatypes                                          *
 *                                                                        *
 *  Software Version: 4.6                                                 *
 *                                                                        *
 *  Release Date    : Fri Aug 19 11:20:11 PDT 2022                        *
 *  Release Type    : Production Release                                  *
 *  Release Build   : 4.6.1                                               *
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
 *  The API remains the same as defined by Mentor Graphics.               *
 *                                                                        *
 *************************************************************************/

/*
//  Source:         ac_channel.h
//  Description:    templatized channel communication class
//  Original Author:  Andres Takach, Ph.D.
//  Modified by:      Fabrizio Ferrandi <fabrizio.ferrandi@polimi.it>
//                    Michele Fiorito <michele.fiorito@polimi.it>
*/

#ifndef __AC_CHANNEL_H
#define __AC_CHANNEL_H

#ifndef __cplusplus
#error C++ is required to include this header file
#endif

#include "ac_compat.h"

#include <deque>
#include <fstream>
#include <initializer_list>
#include <ostream>
#include <string>

#include "ac_fixed.h"
#include "ac_int.h"

// not directly used by this include
#include <cstdio>
#include <cstdlib>

// Macro Definitions (obsolete - provided here for backward compatibility)
#define AC_CHAN_CTOR(varname) varname
#define AC_CHAN_CTOR_INIT(varname, init) varname(init)
#define AC_CHAN_CTOR_VAL(varname, init, val) varname(init, val)

#ifndef __FORCE_INLINE
#define __FORCE_INLINE __attribute__((always_inline)) inline
#endif

////////////////////////////////////////////////
// Struct: ac_exception / ac_channel_exception
////////////////////////////////////////////////

#ifndef __INCLUDED_AC_EXCEPTION
#define __INCLUDED_AC_EXCEPTION
struct ac_exception
{
   const char* const file;
   const unsigned int line;
   const int code;
   const char* const msg;
   ac_exception(const char* file_, const unsigned int& line_, const int& code_, const char* msg_)
       : file(file_), line(line_), code(code_), msg(msg_)
   {
   }
};
#endif

struct ac_channel_exception
{
   enum
   {
      code_begin = 1024
   };
   enum code
   {
      read_from_empty_channel = code_begin,
      fifo_not_empty_when_reset,
      no_operator_sb_defined_for_channel_type,
      no_insert_defined_for_channel_type,
      no_peek_defined_for_channel_type,
      no_size_in_connections,
      no_num_free_in_connections,
      no_output_empty_in_connections,
      write_to_full_channel
   };
   static inline const char* msg(const code& code_)
   {
      static const char* const s[] = {"Read from empty channel",
                                      "fifo not empty when reset",
                                      "No operator[] defined for channel type",
                                      "No insert defined for channel type",
                                      "No peek defined for channel type",
                                      "Connections does not support size()",
                                      "Connections does not support num_free()",
                                      "Connections::Out does not support empty()",
                                      "Write to full channel"};
      return s[code_ - code_begin];
   }
};

///////////////////////////////////////////
// Traits: how T travels over the channel
//////////////////////////////////////////

// Logical width of T on the wire. For a plain type that is its whole object representation; ac_int and
// ac_fixed carry fewer bits than they occupy, and only those go over the channel.
template <class T>
struct ac_channel_packed
{
   enum
   {
      bits = 8 * sizeof(T)
   };
};

template <int W, bool S>
struct ac_channel_packed<ac_int<W, S>>
{
   enum
   {
      bits = W
   };
};

template <int W, int I, bool S, ac_q_mode Q, ac_o_mode O>
struct ac_channel_packed<ac_fixed<W, I, S, Q, O>>
{
   enum
   {
      bits = W
   };
};

// True when the object representation is wider than the logical width, which is the only case where a
// conversion is needed on the blocking path.
template <class T>
struct ac_channel_is_ac
{
   enum
   {
      value = 0
   };
};

template <int W, bool S>
struct ac_channel_is_ac<ac_int<W, S>>
{
   enum
   {
      value = 1
   };
};

template <int W, int I, bool S, ac_q_mode Q, ac_o_mode O>
struct ac_channel_is_ac<ac_fixed<W, I, S, Q, O>>
{
   enum
   {
      value = 1
   };
};

///////////////////////////////////////////
// Class: ac_channel
//////////////////////////////////////////

// One class, one object layout, one body per method - for synthesis and for simulation alike.
//
// The methods split into two families, and neither needs the preprocessor:
//
//  - what bambu rewrites (read, peek, write and the non-blocking forms) has a single body that calls the
//    seam below. The seam is declared here and never defined: during synthesis the prototype survives into
//    the IR as an undefined external, which InterfaceInfer matches by mangled name and replaces with a FIFO
//    port. On the host, ac_channel_sim.h supplies the body, and that body works on the deque.
//
//  - everything else (size, empty, operator[], reset, the constructors, ...) has a single body that works
//    on the deque directly. No seam, nothing to inject. These compile under synthesis too and simply are
//    not called: none of them means anything on a hardware FIFO.
//
// The deque is therefore the only storage, in both flows. Under synthesis the kernel never touches it, so
// it goes away with the constructor once bambu has rewritten the calls.
template <class T>
class ac_channel
{
 public:
   using element_type = T;

   ac_channel();
   ac_channel(const ac_channel<T>&) = default;
   ac_channel(int init);
   ac_channel(int init, T val);
   ac_channel(std::initializer_list<T> val);
   ac_channel(const char* bin_file);

   ac_channel& operator=(const ac_channel<T>&) = default;

   /////////////////////////////////////////////////
   // What bambu rewrites
   /////////////////////////////////////////////////

   // These stay non-template on purpose: they are what absorbs implicit conversions at the call site.
   // Writing an int literal into an ac_channel<ap_uint<16>>, or the widened result of ac_int arithmetic,
   // works only because the parameter is a plain const T&. The payload-typed machinery is private below,
   // where T0 is deduced and a converted argument would fail its SFINAE guard.
   __FORCE_INLINE T read()
   {
      T val;
      _read0(val);
      return val;
   }
   __FORCE_INLINE void read(T& t)
   {
      t = read();
   }
   __FORCE_INLINE bool nb_read(T& t)
   {
      bool res;
      T temp;
      _read0(temp, res);
      t = res ? temp : t;
      return res;
   }

   __FORCE_INLINE T peek()
   {
      T val;
      _peek0(val);
      return val;
   }
   __FORCE_INLINE void peek(T& t)
   {
      t = peek();
   }
   __FORCE_INLINE bool nb_peek(T& t)
   {
      bool res;
      T temp;
      _peek0(temp, res);
      t = res ? temp : t;
      return res;
   }

   __FORCE_INLINE void write(const T& t)
   {
      _write0(t);
   }
   __FORCE_INLINE bool nb_write(T& t)
   {
      return _nb_write0(t);
   }

   /////////////////////////////////////////////////
   // What has no hardware meaning
   /////////////////////////////////////////////////

   __FORCE_INLINE unsigned int size()
   {
      ++size_call_count;
      return static_cast<unsigned int>(ch.size());
   }

   __FORCE_INLINE bool empty()
   {
      return ch.empty();
   }

   __FORCE_INLINE unsigned int num_free() const
   {
      return static_cast<unsigned int>(ch.max_size() - ch.size());
   }

   // Return true if channel has at least k entries
   __FORCE_INLINE bool available(unsigned int k) const
   {
      return ch.size() >= k;
   }

   __FORCE_INLINE void reset()
   {
      ch.clear();
      for(unsigned int i = 0; i < rSz; ++i)
      {
         ch.push_back(rVal);
      }
   }

   __FORCE_INLINE unsigned int debug_size() const
   {
      return static_cast<unsigned int>(ch.size());
   }

   __FORCE_INLINE const T& operator[](unsigned int pos) const
   {
      return ch[pos];
   }

   __FORCE_INLINE T& operator[](unsigned int pos)
   {
      return ch[pos];
   }

   __FORCE_INLINE int get_size_call_count()
   {
      int tmp = size_call_count;
      size_call_count = 0;
      return tmp;
   }

   // obsolete - provided here for backward compatibility with ac_channel
   struct iterator
   {
      iterator operator+(unsigned int pos_) const
      {
         return iterator(itr, pos_);
      }

    private:
      friend class ac_channel;
      iterator(const typename std::deque<T>::iterator& itr_, unsigned int pos = 0) : itr(itr_)
      {
         if(pos)
         {
            itr += pos;
         }
      }
      typename std::deque<T>::iterator itr;
   };
   __FORCE_INLINE iterator begin()
   {
      return iterator(ch.begin());
   }
   __FORCE_INLINE void insert(iterator itr, const T& t)
   {
      ch.insert(itr.itr, t);
   }

   // The SystemC and Connections backends held their own storage as alternate fifo_abstract
   // implementations. A single fifo excludes them by construction; reinstating them means giving the
   // channel a per-backend storage again, which is separate work. Nothing in PandA exercises them, so they
   // are marked rather than ported blind - the previous implementation is in git history, before the
   // channel unification. These fire at instantiation, so a build that never calls bind() is unaffected.
#ifdef SYSTEMC_INCLUDED
   __FORCE_INLINE void bind(sc_core::sc_fifo_in<T>&)
   {
      static_assert(sizeof(T) == 0, "ac_channel: SystemC binding is not supported by this build");
   }
   __FORCE_INLINE void bind(sc_core::sc_fifo_out<T>&)
   {
      static_assert(sizeof(T) == 0, "ac_channel: SystemC binding is not supported by this build");
   }
#endif

#ifdef __CONNECTIONS__CONNECTIONS_H__
   __FORCE_INLINE void bind(Connections::Out<T>&)
   {
      static_assert(sizeof(T) == 0, "ac_channel: Connections binding is not supported by this build");
   }
   __FORCE_INLINE void bind(Connections::In<T>&)
   {
      static_assert(sizeof(T) == 0, "ac_channel: Connections binding is not supported by this build");
   }
   __FORCE_INLINE void bind(Connections::SyncIn&)
   {
      static_assert(sizeof(T) == 0, "ac_channel: Connections binding is not supported by this build");
   }
   __FORCE_INLINE void bind(Connections::SyncOut&)
   {
      static_assert(sizeof(T) == 0, "ac_channel: Connections binding is not supported by this build");
   }
#endif

 private:
   std::deque<T> ch;    // the one fifo
   unsigned int rSz;    // reset size
   T rVal;              // reset value
   int size_call_count;

   /////////////////////////////////////////////////
   // The seam - declared here, defined in ac_channel_sim.h
   /////////////////////////////////////////////////

   // Their mangled names carry both "ac_channel" and "_read_bambu_internal"/"_peek_bambu_internal"/
   // "_write_bambu_internal", which is what InterfaceInfer matches on; the channel object is argument 0
   // and the payload comes last. Bambu takes the payload width from the return type.
   template <class T0>
   const T0 _read_bambu_internal();
   template <class T0>
   const T0 _read_bambu_internal(bool& res);
   template <class T0>
   const T0 _read_bambu_internal(bool& res, bool& dummy);
   template <class T0>
   const T0 _peek_bambu_internal();
   template <class T0>
   const T0 _peek_bambu_internal(bool& res);
   template <class T0>
   const T0 _peek_bambu_internal(bool& res, bool& dummy);
   template <class T0>
   bool _write_bambu_internal(T0 t);

#if __clang_major__ >= 16
   template <class T0, int W>
   union bambu_bitcast_payload
   {
      unsigned _BitInt(W) bits;
      T0 object;

      __FORCE_INLINE bambu_bitcast_payload()
      {
      }
   };
#endif

   /////////////////////////////////////////////////
   // Conversion between T and the payload
   /////////////////////////////////////////////////

   // Needed only where the two representations differ, which measurement narrowed down to two cases:
   //
   //  - ac_int and ac_fixed, whose logical width is narrower than sizeof. Letting one travel as itself
   //    costs 8 cycles out of 18 on ac_channels, and nb_peek does not even build - bambu stops on a
   //    16-bit load it cannot remove.
   //  - the non-blocking form on a plain type, where one extra bit rides in the payload to carry the
   //    valid flag instead of a separate handshake.
   //
   // On the blocking path a plain type travels as itself: T0 is T, the conversion is a copy, and no union
   // is involved - which is also what keeps indeterminate padding out of the round trip.
   template <class T0, std::enable_if_t<std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE T0 _to_payload(const T& t)
   {
      return t;
   }
   template <class T0, std::enable_if_t<std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE void _from_payload(T& t, const T0& bits)
   {
      t = bits;
   }

#if __clang_major__ >= 16
   template <class T0, std::enable_if_t<!std::is_same<T0, T>::value && !ac_channel_is_ac<T>::value, bool> = true>
   static __FORCE_INLINE T0 _to_payload(const T& t)
   {
      bambu_bitcast_payload<T, ac_channel_packed<T>::bits> payload;
      payload.bits = 0;
      payload.object = t;
      return static_cast<T0>(payload.bits);
   }
   template <class T0, std::enable_if_t<!std::is_same<T0, T>::value && !ac_channel_is_ac<T>::value, bool> = true>
   static __FORCE_INLINE void _from_payload(T& t, const T0& bits)
   {
      bambu_bitcast_payload<T, ac_channel_packed<T>::bits> payload;
      payload.bits = static_cast<unsigned _BitInt(ac_channel_packed<T>::bits)>(bits);
      t = payload.object;
   }

   template <class T0, std::enable_if_t<ac_channel_is_ac<T>::value && !std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE T0 _to_payload(const T& t)
   {
      return static_cast<T0>(t.to_BitInt());
   }
   template <class T0, std::enable_if_t<ac_channel_is_ac<T>::value && !std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE void _from_payload(T& t, const T0& bits)
   {
      unsigned _BitInt(ac_channel_packed<T>::bits) val = static_cast<unsigned _BitInt(ac_channel_packed<T>::bits)>(bits);
      t.from_BitInt(val);
   }
#endif

   /////////////////////////////////////////////////
   // Blocking read / peek / write
   /////////////////////////////////////////////////

   // A plain type travels as itself, so there is nothing to convert here.
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _read0(T0& t)
   {
      t = _read_bambu_internal<T0>();
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _peek0(T0& t)
   {
      t = _peek_bambu_internal<T0>();
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _write0(const T0& t)
   {
      _write_bambu_internal(t);
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE bool _nb_write0(const T0& t)
   {
      return _write_bambu_internal(t);
   }

#if __clang_major__ >= 16
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_int<W, S>& t)
   {
      unsigned _BitInt(W) res = _read_bambu_internal<unsigned _BitInt(W)>();
      t.from_BitInt(res);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_fixed<W, I, S, Q, O>& t)
   {
      unsigned _BitInt(W) res = _read_bambu_internal<unsigned _BitInt(W)>();
      t.from_BitInt(res);
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_int<W, S>& t)
   {
      unsigned _BitInt(W) res = _peek_bambu_internal<unsigned _BitInt(W)>();
      t.from_BitInt(res);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_fixed<W, I, S, Q, O>& t)
   {
      unsigned _BitInt(W) res = _peek_bambu_internal<unsigned _BitInt(W)>();
      t.from_BitInt(res);
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _write0(const ac_int<W, S>& t)
   {
      _write_bambu_internal(t.to_BitInt());
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _write0(const ac_fixed<W, I, S, Q, O>& t)
   {
      _write_bambu_internal(t.to_BitInt());
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE bool _nb_write0(const ac_int<W, S>& t)
   {
      return _write_bambu_internal(t.to_BitInt());
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE bool _nb_write0(const ac_fixed<W, I, S, Q, O>& t)
   {
      return _write_bambu_internal(t.to_BitInt());
   }
#endif

   /////////////////////////////////////////////////
   // Non-blocking read / peek
   /////////////////////////////////////////////////

   // The payload asks for one bit more than the data: bits [0, N) carry the value and bit N the valid
   // flag, so the whole answer arrives in one word. This is worth 8 cycles out of 26 on the async
   // benchmarks against the alternative below, which needs a separate flag.
#if __clang_major__ >= 16
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _read0(T0& t, bool& cond)
   {
      enum
      {
         _BitWidth0 = 8 * sizeof(T0)
      };
      bool cond0;
      unsigned _BitInt(_BitWidth0 + 1) res = _read_bambu_internal<unsigned _BitInt(_BitWidth0 + 1)>(cond0);
      unsigned char cond1 = (res >> ((unsigned _BitInt(_BitWidth0 + 1)) _BitWidth0)) & 1;
      cond = cond1;
      bambu_bitcast_payload<T0, _BitWidth0> payload;
      payload.bits = res;
      t = payload.object;
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _peek0(T0& t, bool& cond)
   {
      enum
      {
         _BitWidth0 = 8 * sizeof(T0)
      };
      bool cond0;
      unsigned _BitInt(_BitWidth0 + 1) res = _peek_bambu_internal<unsigned _BitInt(_BitWidth0 + 1)>(cond0);
      unsigned char cond1 = (res >> ((unsigned _BitInt(_BitWidth0 + 1)) _BitWidth0)) & 1;
      cond = cond1;
      bambu_bitcast_payload<T0, _BitWidth0> payload;
      payload.bits = res;
      t = payload.object;
   }

   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_int<W, S>& t, bool& cond)
   {
      bool cond0;
      unsigned _BitInt(W + 1) res = _read_bambu_internal<unsigned _BitInt(W + 1)>(cond0);
      cond = (res >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) val = res;
      t.from_BitInt(val);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_fixed<W, I, S, Q, O>& t, bool& cond)
   {
      bool cond0;
      unsigned _BitInt(W + 1) res = _read_bambu_internal<unsigned _BitInt(W + 1)>(cond0);
      cond = (res >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) val = res;
      t.from_BitInt(val);
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_int<W, S>& t, bool& cond)
   {
      bool cond0;
      unsigned _BitInt(W + 1) res = _peek_bambu_internal<unsigned _BitInt(W + 1)>(cond0);
      cond = (res >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) val = res;
      t.from_BitInt(val);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_fixed<W, I, S, Q, O>& t, bool& cond)
   {
      bool cond0;
      unsigned _BitInt(W + 1) res = _peek_bambu_internal<unsigned _BitInt(W + 1)>(cond0);
      cond = (res >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) val = res;
      t.from_BitInt(val);
   }
#else
   // Without _BitInt the valid flag cannot ride in the payload, so it comes back by reference.
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _read0(T0& t, bool& cond)
   {
      bool dummy;
      t = _read_bambu_internal<T0>(cond, dummy);
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _peek0(T0& t, bool& cond)
   {
      bool dummy;
      t = _peek_bambu_internal<T0>(cond, dummy);
   }
#endif
};

template <class T>
ac_channel<T>::ac_channel() : rSz(0), size_call_count(0)
{
}

template <class T>
ac_channel<T>::ac_channel(int init) : rSz(static_cast<unsigned int>(init)), size_call_count(0)
{
   T dc;
   rVal = dc;
   for(int i = init; i > 0; i--)
   {
      write(dc);
   }
}

template <class T>
ac_channel<T>::ac_channel(int init, T val) : rSz(static_cast<unsigned int>(init)), rVal(val), size_call_count(0)
{
   for(int i = init; i > 0; i--)
   {
      write(val);
   }
}

template <class T>
ac_channel<T>::ac_channel(std::initializer_list<T> val) : rSz(0), size_call_count(0)
{
   for(auto& v : val)
   {
      write(v);
   }
}

template <class T>
ac_channel<T>::ac_channel(const char* bin_file) : rSz(0), size_call_count(0)
{
   std::ifstream init_file(bin_file, std::ifstream::in | std::ifstream::binary);
   T v;
   while(init_file.read((char*)&v, sizeof(T)))
   {
      write(v);
   }
}

template <class T>
__FORCE_INLINE std::ostream& operator<<(std::ostream& os, ac_channel<T>& a)
{
   for(unsigned int i = 0; i < a.size(); i++)
   {
      if(i > 0)
      {
         os << " ";
      }
      os << a[i];
   }
   return os;
}

// This general case is meant to cover non channel (or array of them) args
//   Its result will be ignored
template <typename T>
bool nb_read_chan_rdy(T& x)
{
   return true;
}

template <typename T>
bool nb_read_chan_rdy(ac_channel<T>& chan)
{
   return !chan.empty();
}

template <typename T, int N>
bool nb_read_chan_rdy(ac_channel<T> (&chan)[N])
{
   bool r = true;
   for(int i = 0; i < N; i++)
   {
      r &= !chan[i].empty();
   }
   return r;
}

#if __cplusplus > 199711L
template <typename... Args>
bool nb_read_chan_rdy(Args&... args)
{
   const int n_args = sizeof...(args);
   // only every other arg is a channel (or an array of channels)
   bool rdy[n_args] = {(nb_read_chan_rdy(args))...};
   bool r = true;
   for(int i = 0; i < n_args; i += 2)
   {
      r &= rdy[i];
   }
   return r;
}
#endif

template <typename T>
void nb_read_r(ac_channel<T>& chan, T& var)
{
   chan.nb_read(var);
}

template <typename T, int N>
void nb_read_r(ac_channel<T> (&chan)[N], T (&var)[N])
{
   for(int i = 0; i < N; i++)
   {
      chan[i].nb_read(var[i]);
   }
}

#if __cplusplus > 199711L
template <typename T, typename... Args>
void nb_read_r(ac_channel<T>& chan, T& var, Args&... args)
{
   chan.nb_read(var);
   nb_read_r(args...);
}

template <typename T, int N, typename... Args>
void nb_read_r(ac_channel<T> (&chan)[N], T (&var)[N], Args&... args)
{
   for(int i = 0; i < N; i++)
   {
      chan[i].nb_read(var[i]);
   }
   nb_read_r(args...);
}

template <typename... Args>
bool nb_read_join(Args&... args)
{
   if(nb_read_chan_rdy(args...))
   {
      nb_read_r(args...);
      return true;
   }
   return false;
}
#endif

#endif
