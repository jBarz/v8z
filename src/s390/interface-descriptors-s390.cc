// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_S390

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


const Register LoadDescriptor::ReceiverRegister() { return r3; }
const Register LoadDescriptor::NameRegister() { return r4; }
const Register LoadDescriptor::SlotRegister() { return r2; }


const Register LoadWithVectorDescriptor::VectorRegister() { return r5; }


const Register StoreDescriptor::ReceiverRegister() { return r3; }
const Register StoreDescriptor::NameRegister() { return r4; }
const Register StoreDescriptor::ValueRegister() { return r2; }


const Register VectorStoreICTrampolineDescriptor::SlotRegister() { return r6; }


const Register VectorStoreICDescriptor::VectorRegister() { return r5; }


const Register VectorStoreTransitionDescriptor::SlotRegister() { return r6; }
const Register VectorStoreTransitionDescriptor::VectorRegister() { return r5; }
const Register VectorStoreTransitionDescriptor::MapRegister() { return r7; }


const Register StoreTransitionDescriptor::MapRegister() { return r5; }


const Register LoadGlobalViaContextDescriptor::SlotRegister() { return r4; }


const Register StoreGlobalViaContextDescriptor::SlotRegister() { return r4; }
const Register StoreGlobalViaContextDescriptor::ValueRegister() { return r2; }


const Register InstanceOfDescriptor::LeftRegister() { return r3; }
const Register InstanceOfDescriptor::RightRegister() { return r2; }


const Register StringCompareDescriptor::LeftRegister() { return r3; }
const Register StringCompareDescriptor::RightRegister() { return r2; }


const Register ArgumentsAccessReadDescriptor::index() { return r3; }
const Register ArgumentsAccessReadDescriptor::parameter_count() { return r2; }


const Register ArgumentsAccessNewDescriptor::function() { return r3; }
const Register ArgumentsAccessNewDescriptor::parameter_count() { return r4; }
const Register ArgumentsAccessNewDescriptor::parameter_pointer() { return r5; }


const Register ApiGetterDescriptor::function_address() { return r4; }


const Register MathPowTaggedDescriptor::exponent() { return r4; }


const Register MathPowIntegerDescriptor::exponent() {
  return MathPowTaggedDescriptor::exponent();
}


const Register GrowArrayElementsDescriptor::ObjectRegister() { return r2; }
const Register GrowArrayElementsDescriptor::KeyRegister() { return r5; }


void FastNewClosureDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r4};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastNewContextDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ToNumberDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


// static
const Register ToLengthDescriptor::ReceiverRegister() { return r2; }


// static
const Register ToStringDescriptor::ReceiverRegister() { return r2; }


// static
const Register ToObjectDescriptor::ReceiverRegister() { return r2; }


void NumberToStringDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void TypeofDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r5};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastCloneShallowArrayDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r5, r4, r3};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastCloneShallowObjectDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r5, r4, r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CreateAllocationSiteDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r4, r5};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CreateWeakCellDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r4, r5, r3};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void StoreArrayLiteralElementDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r5, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallFunctionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallFunctionWithFeedbackDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3, r5};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallFunctionWithFeedbackAndVectorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3, r5, r4};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallConstructDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // r2 : number of arguments
  // r3 : the function to call
  // r4 : feedback vector
  // r5 : slot in feedback vector (Smi, for RecordCallTarget)
  // r6 : original constructor (for IsSuperConstructorCall)
  // TODO(turbofan): So far we don't gather type feedback and hence skip the
  // slot parameter, but ArrayConstructStub needs the vector to be undefined.
  Register registers[] = {r2, r3, r6, r4};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // r2 : number of arguments
  // r3 : the target to call
  Register registers[] = {r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void RegExpConstructResultDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r4, r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void TransitionElementsKindDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r2, r3};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void AllocateHeapNumberDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  data->InitializePlatformSpecific(0, nullptr, nullptr);
}


void AllocateInNewSpaceDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ArrayConstructorConstantArgCountDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  // r2 -- number of arguments
  // r3 -- function
  // r4 -- allocation site with elements kind
  Register registers[] = {r3, r4};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ArrayConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {r3, r4, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void InternalArrayConstructorConstantArgCountDescriptor::
    InitializePlatformSpecific(CallInterfaceDescriptorData* data) {
  // register state
  // r2 -- number of arguments
  // r3 -- constructor function
  Register registers[] = {r3};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void InternalArrayConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CompareDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CompareNilDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ToBooleanDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void BinaryOpDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void BinaryOpWithAllocationSiteDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r4, r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void StringAddDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {r3, r2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void KeyedDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r4,  // key
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void NamedDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r4,  // name
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallHandlerDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r2,  // receiver
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ArgumentAdaptorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r3,  // JSFunction
      r2,  // actual number of arguments
      r4,  // expected number of arguments
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ApiFunctionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r2,  // callee
      r6,  // call_data
      r4,  // holder
      r3,  // api_function_address
      r5,  // actual number of arguments
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ApiAccessorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r2,  // callee
      r6,  // call_data
      r4,  // holder
      r3,  // api_function_address
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void MathRoundVariantCallFromUnoptimizedCodeDescriptor::
    InitializePlatformSpecific(CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r3,  // math rounding function
      r5,  // vector slot id
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void MathRoundVariantCallFromOptimizedCodeDescriptor::
    InitializePlatformSpecific(CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r3,  // math rounding function
      r5,  // vector slot id
      r6,  // type vector
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void InterpreterPushArgsAndCallDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r2,  // argument count (not including receiver)
      r4,  // address of first argument
      r3   // the target callable to be call
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void InterpreterPushArgsAndConstructDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r2,  // argument count (not including receiver)
      r5,  // original constructor
      r3,  // constructor to call
      r4   // address of the first argument
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void InterpreterCEntryDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {
      r2,  // argument count (argc)
      r4,  // address of first argument (argv)
      r3   // the runtime function to call
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390
