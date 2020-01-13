/*
 * Copyright © 2012 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * \file half.hpp
 *
 */

#ifndef __GBE_IR_HALF_HPP__
#define __GBE_IR_HALF_HPP__

#include "llvm/ADT/APFloat.h"

namespace gbe {
namespace ir {
  /* Because there is no builtin half float data type for GCC on X86 platform,
     we need to generate a half class to implement all the OP and CONV for half
     float using LLVM's APFloat ADT. */
  class half
  {
    private:
      uint16_t val;
    public:
      explicit half(uint16_t v) : val(v) {};
      static half toHalf(uint16_t u16);
      static half toHalf(int16_t v16);
      uint16_t getVal() { return val; };
      operator float () const;
      operator double () const;
      explicit operator uint16_t () const;
      explicit operator int16_t () const;
      half operator+ (const half &) const;
      half operator- (const half &) const;
      half operator* (const half &) const;
      half operator/ (const half &) const;
      half operator% (const half &) const;
      bool operator> (const half &) const;
      bool operator< (const half &) const;
      bool operator== (const half &) const;
      bool operator!= (const half &) const;
      bool operator>= (const half &) const;
      bool operator<= (const half &) const;
      bool operator&& (const half &) const;
      bool operator|| (const half &) const;
  };
} /* namespace ir */
} /* namespace gbe */
#endif /* End of __GBE_IR_HALF_HPP__ */
