// Copyright (c) 2011-2013, Zeex
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef AMXPROF_FUNCTION_CALL_H
#define AMXPROF_FUNCTION_CALL_H

#include "amx_types.h"
#include "performance_counter.h"

namespace amxprof {

class Function;

class FunctionCall {
 public:
  FunctionCall(Function *function, Address frame, FunctionCall *parent = 0);

  Function *function() { return fn_; }
  const Function *function() const { return fn_; }

  FunctionCall *parent() { return parent_; }
  const FunctionCall *parent() const { return parent_; }

  Address frame() const { return frame_; }

  PerformanceCounter *timer() { return &timer_; }
  const PerformanceCounter *timer() const { return &timer_; }

 private:
  Function *fn_;
  FunctionCall *parent_;
  Address frame_;
  PerformanceCounter timer_;
};

} // namespace amxprof

#endif // !AMXPROF_FUNCTION_CALL_H
