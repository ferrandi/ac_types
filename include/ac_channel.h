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
//                    Tommaso Fellegara <tommaso.fellegara@polimi.it>
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

#include "ac_fixed.h"
#include "ac_int.h"

#if !defined(AC_USER_DEFINED_ASSERT) && !defined(AC_ASSERT_THROW_EXCEPTION)
#include <cassert>
#endif

// not directly used by this include
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
///////////////////////////////////////////

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

// True when the object representation is wider than the logical width, which is the only case where the
// payload has to be built from the value rather than copied from it.
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
///////////////////////////////////////////

// One class and one object layout are shared by synthesis and host simulation:
//
//   FIFO API -> payload adapters -> Bambu ABI seam
//
// During synthesis the seam has no body, so InterfaceInfer can replace its calls with FIFO ports. On
// the host its definitions operate on the deque. Methods outside the FIFO API are software-only and
// access the deque directly. See documentation/ac_channel_abi.md for the complete ABI rationale.
template <class T>
class ac_channel
{
 public:
   using element_type = T;

   /////////////////////////////////////////////////
   // Construction and copying
   /////////////////////////////////////////////////

   ac_channel();
   ac_channel(int init);
   ac_channel(int init, T val);
   ac_channel(std::initializer_list<T> val);
   ac_channel(const char* bin_file);
   ac_channel(const ac_channel<T>&) = default;
   ac_channel& operator=(const ac_channel<T>&) = default;

   /////////////////////////////////////////////////
   // FIFO API: rewritten by Bambu during synthesis
   /////////////////////////////////////////////////

   // Keep this API non-template so implicit conversions happen before the private payload adapters.
   __FORCE_INLINE T read()
   {
      T val;
      _read0(val);
      return val;
   }
   __FORCE_INLINE void read(T& value)
   {
      value = read();
   }
   __FORCE_INLINE bool nb_read(T& value)
   {
      bool valid;
      T temp;
      _read0(temp, valid);
      value = valid ? temp : value;
      return valid;
   }

   __FORCE_INLINE T peek()
   {
      T val;
      _peek0(val);
      return val;
   }
   __FORCE_INLINE void peek(T& value)
   {
      value = peek();
   }
   __FORCE_INLINE bool nb_peek(T& value)
   {
      bool valid;
      T temp;
      _peek0(temp, valid);
      value = valid ? temp : value;
      return valid;
   }

   __FORCE_INLINE void write(const T& value)
   {
      _write0(value);
   }
   __FORCE_INLINE bool nb_write(T& value)
   {
#if !defined(__BAMBU__) || defined(__BAMBU_SIM__)
      ++size_call_count;
#endif
      return _nb_write0(value);
   }

   /////////////////////////////////////////////////
   // Software-only queue inspection and reset
   /////////////////////////////////////////////////

   __FORCE_INLINE unsigned int size()
   {
#if !defined(__BAMBU__) || defined(__BAMBU_SIM__)
      ++size_call_count;
#endif
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

   /////////////////////////////////////////////////
   // Legacy iterator API
   /////////////////////////////////////////////////

   // Obsolete, kept for backward compatibility with ac_channel.
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
   __FORCE_INLINE void insert(iterator itr, const T& value)
   {
      ch.insert(itr.itr, value);
   }

   /////////////////////////////////////////////////
   // Unsupported external backends
   /////////////////////////////////////////////////

   // SystemC and Connections used alternate storage implementations. The unified deque does not support
   // them; fail only when bind() is instantiated. The previous implementation is at git revision abb773b.
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
   /////////////////////////////////////////////////
   // Software state
   /////////////////////////////////////////////////

   std::deque<T> ch; // the one fifo
   unsigned int rSz; // reset size
   T rVal;           // reset value
   int size_call_count;

   /////////////////////////////////////////////////
   // Assertion support
   /////////////////////////////////////////////////

#ifndef AC_CHANNEL_ASSERT
#define AC_CHANNEL_ASSERT(valid, code) ac_assert(valid, __FILE__, __LINE__, code)
   static inline void ac_assert(bool condition, const char* file, int line, const ac_channel_exception::code& code)
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
         // fprintf, not std::cerr: <iostream> is not available in the synthesis build and its static
         // initialiser has no business in the kernel translation unit.
         std::fprintf(stderr, "Assert");
         if(e.file)
         {
            std::fprintf(stderr, " in file %s:%u", e.file, e.line);
         }
         std::fprintf(stderr, " %s\n", e.msg);
         assert(0);
#endif
      }
#else
      AC_USER_DEFINED_ASSERT(condition, file, line, ac_channel_exception::msg(code));
#endif
   }
#else
#error "private use only - AC_CHANNEL_ASSERT macro already defined"
#endif

   /////////////////////////////////////////////////
   // Bambu ABI seam: declarations must keep these signatures
   /////////////////////////////////////////////////

   // InterfaceInfer identifies these member templates by mangled name and takes the payload width from
   // their return type. Host-only definitions follow the class.

   // Blocking read / peek.
   template <class T0>
   const T0 _read_bambu_internal();
   template <class T0>
   const T0 _peek_bambu_internal();

   // Non-blocking read / peek with valid packed above the value bits.
   template <class T0>
   const T0 _read_bambu_internal(bool& valid);
   template <class T0>
   const T0 _peek_bambu_internal(bool& valid);

   // Non-blocking read / peek with valid returned by reference for compilers without _BitInt.
   template <class T0>
   const T0 _read_bambu_internal(bool& valid, bool& dummy);
   template <class T0>
   const T0 _peek_bambu_internal(bool& valid, bool& dummy);

   // Write.
   template <class T0>
   bool _write_bambu_internal(T0 value);

   /////////////////////////////////////////////////
   // Payload bit casting (Clang 16 and later)
   /////////////////////////////////////////////////

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
   // Conversion between channel values and ABI payloads
   /////////////////////////////////////////////////

   // Plain blocking values travel as T. ac_int/ac_fixed use their logical bit width; plain non-blocking
   // values are bit-cast so the valid flag can share the payload. Measurements and padding considerations
   // are documented in documentation/ac_channel_abi.md.
   template <class T0, std::enable_if_t<std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE T0 _to_payload(const T& value)
   {
      return value;
   }
   template <class T0, std::enable_if_t<std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE void _from_payload(T& value, const T0& bits)
   {
      value = bits;
   }

#if __clang_major__ >= 16
   template <class T0, std::enable_if_t<!std::is_same<T0, T>::value && !ac_channel_is_ac<T>::value, bool> = true>
   static __FORCE_INLINE T0 _to_payload(const T& value)
   {
      bambu_bitcast_payload<T, ac_channel_packed<T>::bits> payload;
      payload.bits = 0;
      payload.object = value;
      return static_cast<T0>(payload.bits);
   }
   template <class T0, std::enable_if_t<!std::is_same<T0, T>::value && !ac_channel_is_ac<T>::value, bool> = true>
   static __FORCE_INLINE void _from_payload(T& value, const T0& bits)
   {
      bambu_bitcast_payload<T, ac_channel_packed<T>::bits> payload;
      payload.bits = static_cast<unsigned _BitInt(ac_channel_packed<T>::bits)>(bits);
      value = payload.object;
   }

   template <class T0, std::enable_if_t<ac_channel_is_ac<T>::value && !std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE T0 _to_payload(const T& value)
   {
      return static_cast<T0>(value.to_BitInt());
   }
   template <class T0, std::enable_if_t<ac_channel_is_ac<T>::value && !std::is_same<T0, T>::value, bool> = true>
   static __FORCE_INLINE void _from_payload(T& value, const T0& bits)
   {
      unsigned _BitInt(ac_channel_packed<T>::bits) value_bits =
          static_cast<unsigned _BitInt(ac_channel_packed<T>::bits)>(bits);
      value.from_BitInt(value_bits);
   }
#endif

   /////////////////////////////////////////////////
   // Payload adapters: blocking read / peek / write
   /////////////////////////////////////////////////

   // A plain type travels as itself, so there is nothing to convert here.
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _read0(T0& value)
   {
      value = _read_bambu_internal<T0>();
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _peek0(T0& value)
   {
      value = _peek_bambu_internal<T0>();
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _write0(const T0& value)
   {
      _write_bambu_internal(value);
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE bool _nb_write0(const T0& value)
   {
      return _write_bambu_internal(value);
   }

#if __clang_major__ >= 16
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_int<W, S>& value)
   {
      unsigned _BitInt(W) packed_value = _read_bambu_internal<unsigned _BitInt(W)>();
      value.from_BitInt(packed_value);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_fixed<W, I, S, Q, O>& value)
   {
      unsigned _BitInt(W) packed_value = _read_bambu_internal<unsigned _BitInt(W)>();
      value.from_BitInt(packed_value);
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_int<W, S>& value)
   {
      unsigned _BitInt(W) packed_value = _peek_bambu_internal<unsigned _BitInt(W)>();
      value.from_BitInt(packed_value);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_fixed<W, I, S, Q, O>& value)
   {
      unsigned _BitInt(W) packed_value = _peek_bambu_internal<unsigned _BitInt(W)>();
      value.from_BitInt(packed_value);
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _write0(const ac_int<W, S>& value)
   {
      _write_bambu_internal(value.to_BitInt());
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _write0(const ac_fixed<W, I, S, Q, O>& value)
   {
      _write_bambu_internal(value.to_BitInt());
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE bool _nb_write0(const ac_int<W, S>& value)
   {
      return _write_bambu_internal(value.to_BitInt());
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE bool _nb_write0(const ac_fixed<W, I, S, Q, O>& value)
   {
      return _write_bambu_internal(value.to_BitInt());
   }
#endif

   /////////////////////////////////////////////////
   // Payload adapters: non-blocking read / peek
   /////////////////////////////////////////////////

   // The payload asks for one bit more than the data: bits [0, N) carry the value and bit N the valid
   // flag, so the whole answer arrives in one word.
#if __clang_major__ >= 16
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _read0(T0& value, bool& valid)
   {
      enum
      {
         _BitWidth0 = 8 * sizeof(T0)
      };
      bool ignored_valid;
      unsigned _BitInt(_BitWidth0 + 1) packed_value =
          _read_bambu_internal<unsigned _BitInt(_BitWidth0 + 1)>(ignored_valid);
      unsigned char valid_bit = (packed_value >> ((unsigned _BitInt(_BitWidth0 + 1)) _BitWidth0)) & 1;
      valid = valid_bit;
      bambu_bitcast_payload<T0, _BitWidth0> payload;
      payload.bits = packed_value;
      value = payload.object;
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _peek0(T0& value, bool& valid)
   {
      enum
      {
         _BitWidth0 = 8 * sizeof(T0)
      };
      bool ignored_valid;
      unsigned _BitInt(_BitWidth0 + 1) packed_value =
          _peek_bambu_internal<unsigned _BitInt(_BitWidth0 + 1)>(ignored_valid);
      unsigned char valid_bit = (packed_value >> ((unsigned _BitInt(_BitWidth0 + 1)) _BitWidth0)) & 1;
      valid = valid_bit;
      bambu_bitcast_payload<T0, _BitWidth0> payload;
      payload.bits = packed_value;
      value = payload.object;
   }

   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_int<W, S>& value, bool& valid)
   {
      bool ignored_valid;
      unsigned _BitInt(W + 1) packed_value = _read_bambu_internal<unsigned _BitInt(W + 1)>(ignored_valid);
      valid = (packed_value >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) value_bits = packed_value;
      value.from_BitInt(value_bits);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _read0(ac_fixed<W, I, S, Q, O>& value, bool& valid)
   {
      bool ignored_valid;
      unsigned _BitInt(W + 1) packed_value = _read_bambu_internal<unsigned _BitInt(W + 1)>(ignored_valid);
      valid = (packed_value >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) value_bits = packed_value;
      value.from_BitInt(value_bits);
   }
   template <int W, bool S, std::enable_if_t<std::is_same<T, ac_int<W, S>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_int<W, S>& value, bool& valid)
   {
      bool ignored_valid;
      unsigned _BitInt(W + 1) packed_value = _peek_bambu_internal<unsigned _BitInt(W + 1)>(ignored_valid);
      valid = (packed_value >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) value_bits = packed_value;
      value.from_BitInt(value_bits);
   }
   template <int W, int I, bool S = true, ac_q_mode Q = AC_TRN, ac_o_mode O = AC_WRAP,
             std::enable_if_t<std::is_same<T, ac_fixed<W, I, S, Q, O>>::value, bool> = true>
   __FORCE_INLINE void _peek0(ac_fixed<W, I, S, Q, O>& value, bool& valid)
   {
      bool ignored_valid;
      unsigned _BitInt(W + 1) packed_value = _peek_bambu_internal<unsigned _BitInt(W + 1)>(ignored_valid);
      valid = (packed_value >> ((unsigned _BitInt(W + 1)) W)) & 1;
      unsigned _BitInt(W) value_bits = packed_value;
      value.from_BitInt(value_bits);
   }
#else
   // Without _BitInt the valid flag cannot ride in the payload, so it comes back by reference.
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _read0(T0& value, bool& valid)
   {
      bool dummy;
      value = _read_bambu_internal<T0>(valid, dummy);
   }
   template <class T0, std::enable_if_t<std::is_same<T, T0>::value, bool> = true>
   __FORCE_INLINE void _peek0(T0& value, bool& valid)
   {
      bool dummy;
      value = _peek_bambu_internal<T0>(valid, dummy);
   }
#endif
};

/////////////////////////////////////////////////
// Constructor definitions
/////////////////////////////////////////////////

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
ac_channel<T>::ac_channel(std::initializer_list<T> val)
    : rSz(static_cast<unsigned int>(val.size())), size_call_count(0)
{
   for(auto& v : val)
   {
      write(v);
   }
}

template <class T>
ac_channel<T>::ac_channel(const char* bin_file)
    : rSz(static_cast<unsigned int>(std::ifstream(bin_file, std::ifstream::ate | std::ifstream::binary).tellg() /
                                    sizeof(T))),
      size_call_count(0)
{
   std::ifstream init_file(bin_file, std::ifstream::in | std::ifstream::binary);
   T v;
   while(init_file.read((char*)&v, sizeof(T)))
   {
      write(v);
   }
}

/////////////////////////////////////////////////
// Definitions of the Bambu ABI seam
/////////////////////////////////////////////////

/////////////////////////////////////////////////
// Blocking read / peek
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_read_bambu_internal()
{
   // If you hit this assert you attempted a read on an empty channel. Perhaps you need to guard the
   // execution of the read with a call to the available() function:
   //    if (myInputChan.available(2)) {
   //      // it is safe to read two values
   //      cout << myInputChan.read();
   //      cout << myInputChan.read();
   //    }
   AC_CHANNEL_ASSERT(!ch.empty(), ac_channel_exception::read_from_empty_channel);
   const T0 v = _to_payload<T0>(ch.front());
   ch.pop_front();
   return v;
}

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_peek_bambu_internal()
{
   AC_CHANNEL_ASSERT(!ch.empty(), ac_channel_exception::read_from_empty_channel);
   return _to_payload<T0>(ch.front());
}

/////////////////////////////////////////////////
// Non-blocking read / peek: packed valid bit
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_read_bambu_internal(bool& valid)
{
   valid = !ch.empty();
   if(!valid)
   {
      return static_cast<T0>(0);
   }
   const T0 v = _to_payload<T0>(ch.front());
   ch.pop_front();
   return static_cast<T0>(v | (static_cast<T0>(1) << static_cast<T0>(ac_channel_packed<T>::bits)));
}

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_peek_bambu_internal(bool& valid)
{
   valid = !ch.empty();
   if(!valid)
   {
      return static_cast<T0>(0);
   }
   const T0 v = _to_payload<T0>(ch.front());
   return static_cast<T0>(v | (static_cast<T0>(1) << static_cast<T0>(ac_channel_packed<T>::bits)));
}

/////////////////////////////////////////////////
// Non-blocking read / peek
/////////////////////////////////////////////////

template <class T>
template <class T0>
__attribute__((noinline)) const T0 ac_channel<T>::_read_bambu_internal(bool& valid, bool& dummy)
{
   dummy = false;
   valid = !ch.empty();
   if(!valid)
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
__attribute__((noinline)) const T0 ac_channel<T>::_peek_bambu_internal(bool& valid, bool& dummy)
{
   dummy = false;
   valid = !ch.empty();
   if(!valid)
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
__attribute__((noinline)) bool ac_channel<T>::_write_bambu_internal(T0 value)
{
   AC_CHANNEL_ASSERT(num_free(), ac_channel_exception::write_to_full_channel);
   T v;
   _from_payload(v, value);
   ch.push_back(v);
   return true;
}

/////////////////////////////////////////////////
// Stream and joined-read helpers
/////////////////////////////////////////////////

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

/* undo macro adjustments */
#ifdef AC_CHANNEL_ASSERT
#undef AC_CHANNEL_ASSERT
#endif

#endif
