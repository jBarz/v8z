// Copyright 2012 the V8 project authors. All rights reserved.
//
// Copyright IBM Corp. 2012-2014. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8.h"

#if V8_TARGET_ARCH_S390

#include "ic-inl.h"
#include "codegen.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)


static void ProbeTable(Isolate* isolate,
                       MacroAssembler* masm,
                       Code::Flags flags,
                       StubCache::Table table,
                       Register receiver,
                       Register name,
                       // Number of the cache entry, not scaled.
                       Register offset,
                       Register scratch,
                       Register scratch2,
                       Register offset_scratch) {
  ExternalReference key_offset(isolate->stub_cache()->key_reference(table));
  ExternalReference value_offset(isolate->stub_cache()->value_reference(table));
  ExternalReference map_offset(isolate->stub_cache()->map_reference(table));

  uintptr_t key_off_addr = reinterpret_cast<uintptr_t>(key_offset.address());
  uintptr_t value_off_addr =
                         reinterpret_cast<uintptr_t>(value_offset.address());
  uintptr_t map_off_addr = reinterpret_cast<uintptr_t>(map_offset.address());

  // Check the relative positions of the address fields.
  ASSERT(value_off_addr > key_off_addr);
  ASSERT((value_off_addr - key_off_addr) % 4 == 0);
  ASSERT((value_off_addr - key_off_addr) < (256 * 4));
  ASSERT(map_off_addr > key_off_addr);
  ASSERT((map_off_addr - key_off_addr) % 4 == 0);
  ASSERT((map_off_addr - key_off_addr) < (256 * 4));

  Label miss;
  Register base_addr = scratch;
  scratch = no_reg;

  // Multiply by 3 because there are 3 fields per entry (name, code, map).
  __ ShiftLeftP(offset_scratch, offset, Operand(1));
  __ AddP(offset_scratch, offset);

  // Calculate the base address of the entry.
  __ mov(base_addr, Operand(key_offset));
  __ ShiftLeftP(scratch2, offset_scratch, Operand(kPointerSizeLog2));
  __ AddP(base_addr, scratch2);

  // Check that the key in the entry matches the name.
  __ LoadP(ip, MemOperand(base_addr, 0));
  __ CmpP(name, ip);
  __ bne(&miss);

  // Check the map matches.
  __ LoadP(ip, MemOperand(base_addr, map_off_addr - key_off_addr));
  __ LoadP(scratch2, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ CmpP(ip, scratch2);
  __ bne(&miss);

  // Get the code entry from the cache.
  Register code = scratch2;
  scratch2 = no_reg;
  __ LoadP(code, MemOperand(base_addr, value_off_addr - key_off_addr));

  // Check that the flags match what we're looking for.
  Register flags_reg = base_addr;
  base_addr = no_reg;
  __ LoadlW(flags_reg, FieldMemOperand(code, Code::kFlagsOffset));

  ASSERT(!r0.is(flags_reg));
  __ LoadImmP(r0, Operand(Code::kFlagsNotUsedInLookup));
  __ NotP(r0);
  __ AndP(flags_reg, r0);
  __ mov(r0, Operand(flags));
  __ CmpLogicalP(flags_reg, r0);
  __ bne(&miss);

#ifdef DEBUG
    if (FLAG_test_secondary_stub_cache && table == StubCache::kPrimary) {
      __ b(&miss);
    } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
      __ b(&miss);
    }
#endif

  // Jump to the first instruction in the code stub.
  __ AddP(ip, code, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ b(ip);

  // Miss: fall through.
  __ bind(&miss);
}


void StubCompiler::GenerateDictionaryNegativeLookup(MacroAssembler* masm,
                                                    Label* miss_label,
                                                    Register receiver,
                                                    Handle<Name> name,
                                                    Register scratch0,
                                                    Register scratch1) {
  ASSERT(name->IsUniqueName());
  ASSERT(!receiver.is(scratch0));
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->negative_lookups(), 1, scratch0, scratch1);
  __ IncrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);

  Label done;

  const int kInterceptorOrAccessCheckNeededMask =
      (1 << Map::kHasNamedInterceptor) | (1 << Map::kIsAccessCheckNeeded);

  // Bail out if the receiver has a named interceptor or requires access checks.
  Register map = scratch1;
  __ LoadP(map, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ LoadlB(scratch0, FieldMemOperand(map, Map::kBitFieldOffset));
  __ mov(r0, Operand(kInterceptorOrAccessCheckNeededMask));
  __ AndP(r0, scratch0);
  __ bne(miss_label /*, cr0*/);

  // Check that receiver is a JSObject.
  __ LoadlB(scratch0, FieldMemOperand(map, Map::kInstanceTypeOffset));
  __ CmpP(scratch0, Operand(FIRST_SPEC_OBJECT_TYPE));
  __ blt(miss_label);

  // Load properties array.
  Register properties = scratch0;
  __ LoadP(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));
  // Check that the properties array is a dictionary.
  __ LoadP(map, FieldMemOperand(properties, HeapObject::kMapOffset));
  __ CompareRoot(map, Heap::kHashTableMapRootIndex);
  __ bne(miss_label);

  // Restore the temporarily used register.
  __ LoadP(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));


  NameDictionaryLookupStub::GenerateNegativeLookup(masm,
                                                   miss_label,
                                                   &done,
                                                   receiver,
                                                   properties,
                                                   name,
                                                   scratch1);
  __ bind(&done);
  __ DecrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);
}


void StubCache::GenerateProbe(MacroAssembler* masm,
                              Code::Flags flags,
                              Register receiver,
                              Register name,
                              Register scratch,
                              Register extra,
                              Register extra2,
                              Register extra3) {
  Isolate* isolate = masm->isolate();
  Label miss;

#if V8_TARGET_ARCH_S390X
  // Make sure that code is valid. The multiplying code relies on the
  // entry size being 24.
  ASSERT(sizeof(Entry) == 24);
#else
  // Make sure that code is valid. The multiplying code relies on the
  // entry size being 12.
  ASSERT(sizeof(Entry) == 12);
#endif

  // Make sure the flags does not name a specific type.
  ASSERT(Code::ExtractTypeFromFlags(flags) == 0);

  // Make sure that there are no register conflicts.
  ASSERT(!scratch.is(receiver));
  ASSERT(!scratch.is(name));
  ASSERT(!extra.is(receiver));
  ASSERT(!extra.is(name));
  ASSERT(!extra.is(scratch));
  ASSERT(!extra2.is(receiver));
  ASSERT(!extra2.is(name));
  ASSERT(!extra2.is(scratch));
  ASSERT(!extra2.is(extra));

  // Check scratch, extra and extra2 registers are valid.
  ASSERT(!scratch.is(no_reg));
  ASSERT(!extra.is(no_reg));
  ASSERT(!extra2.is(no_reg));
  ASSERT(!extra3.is(no_reg));

  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->megamorphic_stub_cache_probes(), 1,
                      extra2, extra3);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Get the map of the receiver and compute the hash.
  __ LoadlW(scratch, FieldMemOperand(name, Name::kHashFieldOffset));
  __ LoadP(ip, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ AddP(scratch, ip);
#if V8_TARGET_ARCH_S390X
  // Use only the low 32 bits of the map pointer.
  __ nihf(scratch, Operand::Zero());
#endif
  uint32_t mask = kPrimaryTableSize - 1;
  // We shift out the last two bits because they are not part of the hash and
  // they are always 01 for maps.
  __ ShiftRightP(scratch, scratch, Operand(kHeapObjectTagSize));
  // Mask down the eor argument to the minimum to keep the immediate
  // encodable.
  __ XorP(scratch, Operand((flags >> kHeapObjectTagSize) & mask));
  // Prefer and_ to ubfx here because ubfx takes 2 cycles.
  __ AndP(scratch, Operand(mask));

  // Probe the primary table.
  ProbeTable(isolate,
             masm,
             flags,
             kPrimary,
             receiver,
             name,
             scratch,
             extra,
             extra2,
             extra3);

  // Primary miss: Compute hash for secondary probe.
  __ ShiftRightP(extra, name, Operand(kHeapObjectTagSize));
  __ SubP(scratch, extra);
  uint32_t mask2 = kSecondaryTableSize - 1;
  __ AddP(scratch, Operand((flags >> kHeapObjectTagSize) & mask2));
  __ AndP(scratch, Operand(mask2));

  // Probe the secondary table.
  ProbeTable(isolate,
             masm,
             flags,
             kSecondary,
             receiver,
             name,
             scratch,
             extra,
             extra2,
             extra3);

  // Cache miss: Fall-through and let caller handle the miss by
  // entering the runtime system.
  __ bind(&miss);
  __ IncrementCounter(counters->megamorphic_stub_cache_misses(), 1,
                      extra2, extra3);
}


void StubCompiler::GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                       int index,
                                                       Register prototype) {
  // Load the global or builtins object from the current context.
  __ LoadP(prototype,
           MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the native context from the global or builtins object.
  __ LoadP(prototype,
           FieldMemOperand(prototype, GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  __ LoadP(prototype, MemOperand(prototype, Context::SlotOffset(index)), r0);
  // Load the initial map.  The global functions all have initial maps.
  __ LoadP(prototype,
           FieldMemOperand(prototype,
                           JSFunction::kPrototypeOrInitialMapOffset));
  // Load the prototype from the initial map.
  __ LoadP(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateDirectLoadGlobalFunctionPrototype(
    MacroAssembler* masm,
    int index,
    Register prototype,
    Label* miss) {
  Isolate* isolate = masm->isolate();
  // Get the global function with the given index.
  Handle<JSFunction> function(
      JSFunction::cast(isolate->native_context()->get(index)));

  // Check we're still in the same context.
  Register scratch = prototype;
  const int offset = Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX);
  __ LoadP(scratch, MemOperand(cp, offset));
  __ LoadP(scratch, FieldMemOperand(scratch,
                                    GlobalObject::kNativeContextOffset));
  __ LoadP(scratch, MemOperand(scratch, Context::SlotOffset(index)));
  __ Move(ip, function);
  __ CmpP(ip, scratch);
  __ bne(miss);

  // Load its initial map. The global functions all have initial maps.
  __ Move(prototype, Handle<Map>(function->initial_map()));
  // Load the prototype from the initial map.
  __ LoadP(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateFastPropertyLoad(MacroAssembler* masm,
                                            Register dst,
                                            Register src,
                                            bool inobject,
                                            int index,
                                            Representation representation) {
  ASSERT(!representation.IsDouble());
  int offset = index * kPointerSize;
  if (!inobject) {
    // Calculate the offset into the properties array.
    offset = offset + FixedArray::kHeaderSize;
    __ LoadP(dst, FieldMemOperand(src, JSObject::kPropertiesOffset));
    src = dst;
  }
  __ LoadP(dst, FieldMemOperand(src, offset), r0);
}


void StubCompiler::GenerateLoadArrayLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss_label);

  // Check that the object is a JS array.
  __ CompareObjectType(receiver, scratch, scratch, JS_ARRAY_TYPE);
  __ bne(miss_label);

  // Load length directly from the JS array.
  __ LoadP(r2, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ Ret();
}


void StubCompiler::GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* miss_label) {
  __ TryGetFunctionPrototype(receiver, scratch1, scratch2, miss_label);
  __ LoadRR(r2, scratch1);
  __ Ret();
}


// Generate code to check that a global property cell is empty. Create
// the property cell at compilation time if no cell exists for the
// property.
void StubCompiler::GenerateCheckPropertyCell(MacroAssembler* masm,
                                             Handle<JSGlobalObject> global,
                                             Handle<Name> name,
                                             Register scratch,
                                             Label* miss) {
  Handle<Cell> cell = JSGlobalObject::EnsurePropertyCell(global, name);
  ASSERT(cell->value()->IsTheHole());
  __ mov(scratch, Operand(cell));
  __ LoadP(scratch, FieldMemOperand(scratch, Cell::kValueOffset));
  __ CompareRoot(scratch, Heap::kTheHoleValueRootIndex);
  __ bne(miss);
}


void StoreStubCompiler::GenerateNegativeHolderLookup(
    MacroAssembler* masm,
    Handle<JSObject> holder,
    Register holder_reg,
    Handle<Name> name,
    Label* miss) {
  if (holder->IsJSGlobalObject()) {
    GenerateCheckPropertyCell(
        masm, Handle<JSGlobalObject>::cast(holder), name, scratch1(), miss);
  } else if (!holder->HasFastProperties() && !holder->IsJSGlobalProxy()) {
    GenerateDictionaryNegativeLookup(
        masm, miss, holder_reg, name, scratch1(), scratch2());
  }
}


// Generate StoreTransition code, value is passed in r2 register.
// When leaving generated code after success, the receiver_reg and name_reg
// may be clobbered.  Upon branch to miss_label, the receiver and name
// registers have their original values.
void StoreStubCompiler::GenerateStoreTransition(MacroAssembler* masm,
                                                Handle<JSObject> object,
                                                LookupResult* lookup,
                                                Handle<Map> transition,
                                                Handle<Name> name,
                                                Register receiver_reg,
                                                Register storage_reg,
                                                Register value_reg,
                                                Register scratch1,
                                                Register scratch2,
                                                Register scratch3,
                                                Label* miss_label,
                                                Label* slow) {
  // r2 : value
  Label exit;

  int descriptor = transition->LastAdded();
  DescriptorArray* descriptors = transition->instance_descriptors();
  PropertyDetails details = descriptors->GetDetails(descriptor);
  Representation representation = details.representation();
  ASSERT(!representation.IsNone());

  if (details.type() == CONSTANT) {
    Handle<Object> constant(descriptors->GetValue(descriptor), masm->isolate());
    __ Move(scratch1, constant);
    __ CmpP(value_reg, scratch1);
    __ bne(miss_label);
  } else if (representation.IsSmi()) {
    __ JumpIfNotSmi(value_reg, miss_label);
  } else if (representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
    HeapType* field_type = descriptors->GetFieldType(descriptor);
    HeapType::Iterator<Map> it = field_type->Classes();
    if (!it.Done()) {
      __ LoadP(scratch1, FieldMemOperand(value_reg, HeapObject::kMapOffset));
      Label do_store;
      while (true) {
        __ CompareMap(value_reg, it.Current(), &do_store);
        it.Advance();
        if (it.Done()) {
          __ bne(miss_label);
          break;
        }
        __ beq(&do_store, Label::kNear);
      }
      __ bind(&do_store);
    }
  } else if (representation.IsDouble()) {
    Label do_store, heap_number;
    __ LoadRoot(scratch3, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(storage_reg, scratch1, scratch2, scratch3, slow);

    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntag(scratch1, value_reg);
    __ ConvertIntToDouble(scratch1, d0);
    __ jmp(&do_store, Label::kNear);

    __ bind(&heap_number);
    __ CheckMap(value_reg, scratch1, Heap::kHeapNumberMapRootIndex,
                miss_label, DONT_DO_SMI_CHECK);
    __ LoadF(d0, FieldMemOperand(value_reg, HeapNumber::kValueOffset));

    __ bind(&do_store);
    __ StoreF(d0, FieldMemOperand(storage_reg, HeapNumber::kValueOffset));
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  // Perform map transition for the receiver if necessary.
  if (details.type() == FIELD &&
      object->map()->unused_property_fields() == 0) {
    // The properties must be extended before we can store the value.
    // We jump to a runtime call that extends the properties array.
    __ push(receiver_reg);
    __ mov(r4, Operand(transition));
    __ Push(r4, r2);
    __ TailCallExternalReference(
        ExternalReference(IC_Utility(IC::kSharedStoreIC_ExtendStorage),
                          masm->isolate()),
        3,
        1);
    return;
  }

  // Update the map of the object.
  __ mov(scratch1, Operand(transition));
  __ StoreP(scratch1, FieldMemOperand(receiver_reg, HeapObject::kMapOffset));

  // Update the write barrier for the map field.
  __ RecordWriteField(receiver_reg,
                      HeapObject::kMapOffset,
                      scratch1,
                      scratch2,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  if (details.type() == CONSTANT) {
    ASSERT(value_reg.is(r2));
    __ Ret();
    return;
  }

  int index = transition->instance_descriptors()->GetFieldIndex(
      transition->LastAdded());

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    if (representation.IsDouble()) {
      __ StoreP(storage_reg, FieldMemOperand(receiver_reg, offset));
    } else {
      __ StoreP(value_reg, FieldMemOperand(receiver_reg, offset));
    }

    if (!representation.IsSmi()) {
    // Update the write barrier for the array address.
      if (!representation.IsDouble()) {
        __ LoadRR(storage_reg, value_reg);
      }
      __ RecordWriteField(receiver_reg,
                          offset,
                          storage_reg,
                          scratch1,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ LoadP(scratch1,
             FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    if (representation.IsDouble()) {
      __ StoreP(storage_reg, FieldMemOperand(scratch1, offset));
    } else {
      __ StoreP(value_reg, FieldMemOperand(scratch1, offset));
    }

    if (!representation.IsSmi()) {
      // Update the write barrier for the array address.
      if (!representation.IsDouble()) {
        __ LoadRR(storage_reg, value_reg);
      }
      __ RecordWriteField(scratch1,
                          offset,
                          storage_reg,
                          receiver_reg,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  // Return the value (register r2).
  ASSERT(value_reg.is(r2));
  __ bind(&exit);
  __ Ret();
}


// Generate StoreField code, value is passed in r2 register.
// When leaving generated code after success, the receiver_reg and name_reg
// may be clobbered.  Upon branch to miss_label, the receiver and name
// registers have their original values.
void StoreStubCompiler::GenerateStoreField(MacroAssembler* masm,
                                           Handle<JSObject> object,
                                           LookupResult* lookup,
                                           Register receiver_reg,
                                           Register name_reg,
                                           Register value_reg,
                                           Register scratch1,
                                           Register scratch2,
                                           Label* miss_label) {
  // r2 : value
  Label exit;

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  int index = lookup->GetFieldIndex().field_index();

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  Representation representation = lookup->representation();
  ASSERT(!representation.IsNone());
  if (representation.IsSmi()) {
    __ JumpIfNotSmi(value_reg, miss_label);
  } else if (representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
    HeapType* field_type = lookup->GetFieldType();
    HeapType::Iterator<Map> it = field_type->Classes();
    if (!it.Done()) {
      __ LoadP(scratch1, FieldMemOperand(value_reg, HeapObject::kMapOffset));
      Label do_store;
      while (true) {
        __ CompareMap(value_reg, it.Current(), &do_store);
        it.Advance();
        if (it.Done()) {
          __ bne(miss_label);
          break;
        }
        __ beq(&do_store);
      }
      __ bind(&do_store);
    }
  } else if (representation.IsDouble()) {
    // Load the double storage.
    if (index < 0) {
      int offset = object->map()->instance_size() + (index * kPointerSize);
      __ LoadP(scratch1, FieldMemOperand(receiver_reg, offset));
    } else {
      __ LoadP(scratch1,
             FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
      int offset = index * kPointerSize + FixedArray::kHeaderSize;
      __ LoadP(scratch1, FieldMemOperand(scratch1, offset));
    }

    // Store the value into the storage.
    Label do_store, heap_number;
    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntag(scratch2, value_reg);
    __ ConvertIntToDouble(scratch2, d0);
    __ jmp(&do_store, Label::kNear);

    __ bind(&heap_number);
    __ CheckMap(value_reg, scratch2, Heap::kHeapNumberMapRootIndex,
                miss_label, DONT_DO_SMI_CHECK);
    __ LoadF(d0, FieldMemOperand(value_reg, HeapNumber::kValueOffset));

    __ bind(&do_store);
    __ StoreF(d0, FieldMemOperand(scratch1, HeapNumber::kValueOffset));
    // Return the value (register r2).
    ASSERT(value_reg.is(r2));
    __ Ret();
    return;
  }

  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    __ StoreP(value_reg, FieldMemOperand(receiver_reg, offset));

    if (!representation.IsSmi()) {
      // Skip updating write barrier if storing a smi.
      __ JumpIfSmi(value_reg, &exit);

      // Update the write barrier for the array address.
      // Pass the now unused name_reg as a scratch register.
      __ LoadRR(name_reg, value_reg);
      __ RecordWriteField(receiver_reg,
                          offset,
                          name_reg,
                          scratch1,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ LoadP(scratch1,
           FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    __ StoreP(value_reg, FieldMemOperand(scratch1, offset));

    if (!representation.IsSmi()) {
      // Skip updating write barrier if storing a smi.
      __ JumpIfSmi(value_reg, &exit);

      // Update the write barrier for the array address.
      // Ok to clobber receiver_reg and name_reg, since we return.
      __ LoadRR(name_reg, value_reg);
      __ RecordWriteField(scratch1,
                          offset,
                          name_reg,
                          receiver_reg,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  // Return the value (register r2).
  ASSERT(value_reg.is(r2));
  __ bind(&exit);
  __ Ret();
}


void StoreStubCompiler::GenerateRestoreName(MacroAssembler* masm,
                                            Label* label,
                                            Handle<Name> name) {
  if (!label->is_unused()) {
    __ bind(label);
    __ mov(this->name(), Operand(name));
  }
}


static void PushInterceptorArguments(MacroAssembler* masm,
                                     Register receiver,
                                     Register holder,
                                     Register name,
                                     Handle<JSObject> holder_obj) {
  STATIC_ASSERT(StubCache::kInterceptorArgsNameIndex == 0);
  STATIC_ASSERT(StubCache::kInterceptorArgsInfoIndex == 1);
  STATIC_ASSERT(StubCache::kInterceptorArgsThisIndex == 2);
  STATIC_ASSERT(StubCache::kInterceptorArgsHolderIndex == 3);
  STATIC_ASSERT(StubCache::kInterceptorArgsLength == 4);
  __ push(name);
  Handle<InterceptorInfo> interceptor(holder_obj->GetNamedInterceptor());
  ASSERT(!masm->isolate()->heap()->InNewSpace(*interceptor));
  Register scratch = name;
  __ mov(scratch, Operand(interceptor));
  __ push(scratch);
  __ push(receiver);
  __ push(holder);
}


static void CompileCallLoadPropertyWithInterceptor(
    MacroAssembler* masm,
    Register receiver,
    Register holder,
    Register name,
    Handle<JSObject> holder_obj,
    IC::UtilityId id) {
  PushInterceptorArguments(masm, receiver, holder, name, holder_obj);
  __ CallExternalReference(
      ExternalReference(IC_Utility(id), masm->isolate()),
      StubCache::kInterceptorArgsLength);
}


// Generate call to api function.
void StubCompiler::GenerateFastApiCall(MacroAssembler* masm,
                                       const CallOptimization& optimization,
                                       Handle<Map> receiver_map,
                                       Register receiver,
                                       Register scratch_in,
                                       bool is_store,
                                       int argc,
                                       Register* values) {
  ASSERT(!receiver.is(scratch_in));
  __ push(receiver);
  // Write the arguments to stack frame.
  for (int i = 0; i < argc; i++) {
    Register arg = values[argc-1-i];
    ASSERT(!receiver.is(arg));
    ASSERT(!scratch_in.is(arg));
    __ push(arg);
  }
  ASSERT(optimization.is_simple_api_call());

  // Abi for CallApiFunctionStub.
  Register callee = r2;
  Register call_data = r6;
  Register holder = r4;
  Register api_function_address = r3;

  // Put holder in place.
  CallOptimization::HolderLookup holder_lookup;
  Handle<JSObject> api_holder = optimization.LookupHolderOfExpectedType(
      receiver_map,
      &holder_lookup);
  switch (holder_lookup) {
    case CallOptimization::kHolderIsReceiver:
      __ Move(holder, receiver);
      break;
    case CallOptimization::kHolderFound:
      __ Move(holder, api_holder);
     break;
    case CallOptimization::kHolderNotFound:
      UNREACHABLE();
      break;
  }

  Isolate* isolate = masm->isolate();
  Handle<JSFunction> function = optimization.constant_function();
  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();
  Handle<Object> call_data_obj(api_call_info->data(), isolate);

  // Put callee in place.
  __ Move(callee, function);

  bool call_data_undefined = false;
  // Put call_data in place.
  if (isolate->heap()->InNewSpace(*call_data_obj)) {
    __ Move(call_data, api_call_info);
    __ LoadP(call_data, FieldMemOperand(call_data,
                                        CallHandlerInfo::kDataOffset));
  } else if (call_data_obj->IsUndefined()) {
    call_data_undefined = true;
    __ LoadRoot(call_data, Heap::kUndefinedValueRootIndex);
  } else {
    __ Move(call_data, call_data_obj);
  }

  // Put api_function_address in place.
  Address function_address = v8::ToCData<Address>(api_call_info->callback());
  ApiFunction fun(function_address);
  ExternalReference::Type type = ExternalReference::DIRECT_API_CALL;
  ExternalReference ref = ExternalReference(&fun,
                                            type,
                                            masm->isolate());
  __ mov(api_function_address, Operand(ref));

  // Jump to stub.
  CallApiFunctionStub stub(isolate, is_store, call_data_undefined, argc);
  __ TailCallStub(&stub);
}


void StubCompiler::GenerateTailCall(MacroAssembler* masm, Handle<Code> code) {
  __ Jump(code, RelocInfo::CODE_TARGET);
}


#undef __
#define __ ACCESS_MASM(masm())


Register StubCompiler::CheckPrototypes(Handle<HeapType> type,
                                       Register object_reg,
                                       Handle<JSObject> holder,
                                       Register holder_reg,
                                       Register scratch1,
                                       Register scratch2,
                                       Handle<Name> name,
                                       Label* miss,
                                       PrototypeCheckType check) {
  Handle<Map> receiver_map(IC::TypeToMap(*type, isolate()));

  // Make sure there's no overlap between holder and object registers.
  ASSERT(!scratch1.is(object_reg) && !scratch1.is(holder_reg));
  ASSERT(!scratch2.is(object_reg) && !scratch2.is(holder_reg)
         && !scratch2.is(scratch1));

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 0;

  Handle<JSObject> current = Handle<JSObject>::null();
  if (type->IsConstant()) {
    current = Handle<JSObject>::cast(type->AsConstant()->Value());
  }
  Handle<JSObject> prototype = Handle<JSObject>::null();
  Handle<Map> current_map = receiver_map;
  Handle<Map> holder_map(holder->map());
  // Traverse the prototype chain and check the maps in the prototype chain for
  // fast and global objects or do negative lookup for normal objects.
  while (!current_map.is_identical_to(holder_map)) {
    ++depth;

    // Only global objects and objects that do not require access
    // checks are allowed in stubs.
    ASSERT(current_map->IsJSGlobalProxyMap() ||
           !current_map->is_access_check_needed());

    prototype = handle(JSObject::cast(current_map->prototype()));
    if (current_map->is_dictionary_map() &&
        !current_map->IsJSGlobalObjectMap() &&
        !current_map->IsJSGlobalProxyMap()) {
      if (!name->IsUniqueName()) {
        ASSERT(name->IsString());
        name = factory()->InternalizeString(Handle<String>::cast(name));
      }
      ASSERT(current.is_null() ||
             current->property_dictionary()->FindEntry(name) ==
             NameDictionary::kNotFound);

      GenerateDictionaryNegativeLookup(masm(), miss, reg, name,
                                       scratch1, scratch2);

      __ LoadP(scratch1, FieldMemOperand(reg, HeapObject::kMapOffset));
      reg = holder_reg;  // From now on the object will be in holder_reg.
      __ LoadP(reg, FieldMemOperand(scratch1, Map::kPrototypeOffset));
    } else {
      Register map_reg = scratch1;
      if (depth != 1 || check == CHECK_ALL_MAPS) {
        // CheckMap implicitly loads the map of |reg| into |map_reg|.
        __ CheckMap(reg, map_reg, current_map, miss, DONT_DO_SMI_CHECK);
      } else {
        __ LoadP(map_reg, FieldMemOperand(reg, HeapObject::kMapOffset));
      }

      // Check access rights to the global object.  This has to happen after
      // the map check so that we know that the object is actually a global
      // object.
      if (current_map->IsJSGlobalProxyMap()) {
        __ CheckAccessGlobalProxy(reg, scratch2, miss);
      } else if (current_map->IsJSGlobalObjectMap()) {
        GenerateCheckPropertyCell(
            masm(), Handle<JSGlobalObject>::cast(current), name,
            scratch2, miss);
      }

      reg = holder_reg;  // From now on the object will be in holder_reg.

      if (heap()->InNewSpace(*prototype)) {
        // The prototype is in new space; we cannot store a reference to it
        // in the code.  Load it from the map.
        __ LoadP(reg, FieldMemOperand(map_reg, Map::kPrototypeOffset));
      } else {
        // The prototype is in old space; load it directly.
        __ mov(reg, Operand(prototype));
      }
    }

    // Go to the next object in the prototype chain.
    current = prototype;
    current_map = handle(current->map());
  }

  // Log the check depth.
  LOG(isolate(), IntEvent("check-maps-depth", depth + 1));

  if (depth != 0 || check == CHECK_ALL_MAPS) {
    // Check the holder map.
    __ CheckMap(reg, scratch1, current_map, miss, DONT_DO_SMI_CHECK);
  }

  // Perform security check for access to the global object.
  ASSERT(current_map->IsJSGlobalProxyMap() ||
         !current_map->is_access_check_needed());
  if (current_map->IsJSGlobalProxyMap()) {
    __ CheckAccessGlobalProxy(reg, scratch1, miss);
  }

  // Return the register containing the holder.
  return reg;
}


void LoadStubCompiler::HandlerFrontendFooter(Handle<Name> name, Label* miss) {
  if (!miss->is_unused()) {
    Label success;
    __ b(&success, Label::kNear);
    __ bind(miss);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
    __ bind(&success);
  }
}


void StoreStubCompiler::HandlerFrontendFooter(Handle<Name> name, Label* miss) {
  if (!miss->is_unused()) {
    Label success;
    __ b(&success, Label::kNear);
    GenerateRestoreName(masm(), miss, name);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
    __ bind(&success);
  }
}


Register LoadStubCompiler::CallbackHandlerFrontend(
    Handle<HeapType> type,
    Register object_reg,
    Handle<JSObject> holder,
    Handle<Name> name,
    Handle<Object> callback) {
  Label miss;

  Register reg = HandlerFrontendHeader(type, object_reg, holder, name, &miss);

  if (!holder->HasFastProperties() && !holder->IsJSGlobalObject()) {
    ASSERT(!reg.is(scratch2()));
    ASSERT(!reg.is(scratch3()));
    ASSERT(!reg.is(scratch4()));

    // Load the properties dictionary.
    Register dictionary = scratch4();
    __ LoadP(dictionary, FieldMemOperand(reg, JSObject::kPropertiesOffset));

    // Probe the dictionary.
    Label probe_done;
    NameDictionaryLookupStub::GeneratePositiveLookup(masm(),
                                                     &miss,
                                                     &probe_done,
                                                     dictionary,
                                                     this->name(),
                                                     scratch2(),
                                                     scratch3());
    __ bind(&probe_done);

    // If probing finds an entry in the dictionary, scratch3 contains the
    // pointer into the dictionary. Check that the value is the callback.
    Register pointer = scratch3();
    const int kElementsStartOffset = NameDictionary::kHeaderSize +
        NameDictionary::kElementsStartIndex * kPointerSize;
    const int kValueOffset = kElementsStartOffset + kPointerSize;
    __ LoadP(scratch2(), FieldMemOperand(pointer, kValueOffset));
    __ mov(scratch3(), Operand(callback));
    __ CmpP(scratch2(), scratch3());
    __ bne(&miss);
  }

  HandlerFrontendFooter(name, &miss);
  return reg;
}


void LoadStubCompiler::GenerateLoadField(Register reg,
                                         Handle<JSObject> holder,
                                         PropertyIndex field,
                                         Representation representation) {
  if (!reg.is(receiver())) __ LoadRR(receiver(), reg);
  if (kind() == Code::LOAD_IC) {
    LoadFieldStub stub(isolate(),
                       field.is_inobject(holder),
                       field.translate(holder),
                       representation);
    GenerateTailCall(masm(), stub.GetCode());
  } else {
    KeyedLoadFieldStub stub(isolate(),
                            field.is_inobject(holder),
                            field.translate(holder),
                            representation);
    GenerateTailCall(masm(), stub.GetCode());
  }
}


void LoadStubCompiler::GenerateLoadConstant(Handle<Object> value) {
  // Return the constant value.
  __ Move(r2, value);
  __ Ret();
}


void LoadStubCompiler::GenerateLoadCallback(
    Register reg,
    Handle<ExecutableAccessorInfo> callback) {
  // Build AccessorInfo::args_ list on the stack and push property name below
  // the exit frame to make GC aware of them and store pointers to them.
  STATIC_ASSERT(PropertyCallbackArguments::kHolderIndex == 0);
  STATIC_ASSERT(PropertyCallbackArguments::kIsolateIndex == 1);
  STATIC_ASSERT(PropertyCallbackArguments::kReturnValueDefaultValueIndex == 2);
  STATIC_ASSERT(PropertyCallbackArguments::kReturnValueOffset == 3);
  STATIC_ASSERT(PropertyCallbackArguments::kDataIndex == 4);
  STATIC_ASSERT(PropertyCallbackArguments::kThisIndex == 5);
  STATIC_ASSERT(PropertyCallbackArguments::kArgsLength == 6);
  ASSERT(!scratch2().is(reg));
  ASSERT(!scratch3().is(reg));
  ASSERT(!scratch4().is(reg));
  __ push(receiver());
  if (heap()->InNewSpace(callback->data())) {
    __ Move(scratch3(), callback);
    __ LoadP(scratch3(), FieldMemOperand(scratch3(),
                                       ExecutableAccessorInfo::kDataOffset));
  } else {
    __ Move(scratch3(), Handle<Object>(callback->data(), isolate()));
  }
  __ push(scratch3());
  __ LoadRoot(scratch3(), Heap::kUndefinedValueRootIndex);
  __ LoadRR(scratch4(), scratch3());
  __ Push(scratch3(), scratch4());
  __ mov(scratch4(),
         Operand(ExternalReference::isolate_address(isolate())));
  __ Push(scratch4(), reg);
  __ push(name());

  // Abi for CallApiGetter
  Register getter_address_reg = r4;

  Address getter_address = v8::ToCData<Address>(callback->getter());
  ApiFunction fun(getter_address);
  ExternalReference::Type type = ExternalReference::DIRECT_GETTER_CALL;
  ExternalReference ref = ExternalReference(&fun, type, isolate());
  __ mov(getter_address_reg, Operand(ref));

  CallApiGetterStub stub(isolate());
  __ TailCallStub(&stub);
}


void LoadStubCompiler::GenerateLoadInterceptor(
    Register holder_reg,
    Handle<Object> object,
    Handle<JSObject> interceptor_holder,
    LookupResult* lookup,
    Handle<Name> name) {
  ASSERT(interceptor_holder->HasNamedInterceptor());
  ASSERT(!interceptor_holder->GetNamedInterceptor()->getter()->IsUndefined());

  // So far the most popular follow ups for interceptor loads are FIELD
  // and CALLBACKS, so inline only them, other cases may be added
  // later.
  bool compile_followup_inline = false;
  if (lookup->IsFound() && lookup->IsCacheable()) {
    if (lookup->IsField()) {
      compile_followup_inline = true;
    } else if (lookup->type() == CALLBACKS &&
               lookup->GetCallbackObject()->IsExecutableAccessorInfo()) {
      ExecutableAccessorInfo* callback =
          ExecutableAccessorInfo::cast(lookup->GetCallbackObject());
      compile_followup_inline = callback->getter() != NULL &&
          callback->IsCompatibleReceiver(*object);
    }
  }

  if (compile_followup_inline) {
    // Compile the interceptor call, followed by inline code to load the
    // property from further up the prototype chain if the call fails.
    // Check that the maps haven't changed.
    ASSERT(holder_reg.is(receiver()) || holder_reg.is(scratch1()));

    // Preserve the receiver register explicitly whenever it is different from
    // the holder and it is needed should the interceptor return without any
    // result. The CALLBACKS case needs the receiver to be passed into C++ code,
    // the FIELD case might cause a miss during the prototype check.
    bool must_perfrom_prototype_check = *interceptor_holder != lookup->holder();
    bool must_preserve_receiver_reg = !receiver().is(holder_reg) &&
        (lookup->type() == CALLBACKS || must_perfrom_prototype_check);

    // Save necessary data before invoking an interceptor.
    // Requires a frame to make GC aware of pushed pointers.
    {
      FrameAndConstantPoolScope frame_scope(masm(), StackFrame::INTERNAL);
      if (must_preserve_receiver_reg) {
        __ Push(receiver(), holder_reg, this->name());
      } else {
        __ Push(holder_reg, this->name());
      }
      // Invoke an interceptor.  Note: map checks from receiver to
      // interceptor's holder has been compiled before (see a caller
      // of this method.)
      CompileCallLoadPropertyWithInterceptor(
          masm(), receiver(), holder_reg, this->name(), interceptor_holder,
          IC::kLoadPropertyWithInterceptorOnly);

      // Check if interceptor provided a value for property.  If it's
      // the case, return immediately.
      Label interceptor_failed;
      __ CompareRoot(r2, Heap::kNoInterceptorResultSentinelRootIndex);
      __ beq(&interceptor_failed);
      frame_scope.GenerateLeaveFrame();
      __ Ret();

      __ bind(&interceptor_failed);
      __ pop(this->name());
      __ pop(holder_reg);
      if (must_preserve_receiver_reg) {
        __ pop(receiver());
      }
      // Leave the internal frame.
    }

    GenerateLoadPostInterceptor(holder_reg, interceptor_holder, name, lookup);
  } else {  // !compile_followup_inline
    // Call the runtime system to load the interceptor.
    // Check that the maps haven't changed.
    PushInterceptorArguments(masm(), receiver(), holder_reg,
                             this->name(), interceptor_holder);

    ExternalReference ref =
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForLoad),
                          isolate());
    __ TailCallExternalReference(ref, StubCache::kInterceptorArgsLength, 1);
  }
}


void StubCompiler::GenerateBooleanCheck(Register object, Label* miss) {
  Label success;
  // Check that the object is a boolean.
  __ CompareRoot(object, Heap::kTrueValueRootIndex);
  __ beq(&success, Label::kNear);
  __ CompareRoot(object, Heap::kFalseValueRootIndex);
  __ bne(miss);
  __ bind(&success);
}


Handle<Code> StoreStubCompiler::CompileStoreCallback(
    Handle<JSObject> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    Handle<ExecutableAccessorInfo> callback) {
  Register holder_reg = HandlerFrontend(
      IC::CurrentTypeOf(object, isolate()), receiver(), holder, name);

  // Stub never generated for non-global objects that require access checks.
  ASSERT(holder->IsJSGlobalProxy() || !holder->IsAccessCheckNeeded());

  __ Push(receiver(), holder_reg);  // receiver
  __ mov(ip, Operand(callback));  // callback info
  __ push(ip);
  __ mov(ip, Operand(name));
  __ Push(ip, value());

  // Do tail-call to the runtime system.
  ExternalReference store_callback_property =
      ExternalReference(IC_Utility(IC::kStoreCallbackProperty), isolate());
  __ TailCallExternalReference(store_callback_property, 5, 1);

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


#undef __
#define __ ACCESS_MASM(masm)


void StoreStubCompiler::GenerateStoreViaSetter(
    MacroAssembler* masm,
    Handle<HeapType> type,
    Register receiver,
    Handle<JSFunction> setter) {
  // ----------- S t a t e -------------
  //  -- lr    : return address
  // -----------------------------------
  {
    FrameAndConstantPoolScope scope(masm, StackFrame::INTERNAL);

    // Save value register, so we can restore it later.
    __ push(value());

    if (!setter.is_null()) {
      // Call the JavaScript setter with receiver and value on the stack.
      if (IC::TypeToMap(*type, masm->isolate())->IsJSGlobalObjectMap()) {
        // Swap in the global receiver.
        __ LoadP(receiver,
                 FieldMemOperand(
                   receiver, JSGlobalObject::kGlobalReceiverOffset));
      }
      __ Push(receiver, value());
      ParameterCount actual(1);
      ParameterCount expected(setter);
      __ InvokeFunction(setter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper());
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetSetterStubDeoptPCOffset(masm->pc_offset());
    }

    // We have to return the passed value, not the return value of the setter.
    __ pop(r2);

    // Restore context register.
    __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> StoreStubCompiler::CompileStoreInterceptor(
    Handle<JSObject> object,
    Handle<Name> name) {
  __ Push(receiver(), this->name(), value());

  // Do tail-call to the runtime system.
  ExternalReference store_ic_property =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty), isolate());
  __ TailCallExternalReference(store_ic_property, 3, 1);

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


Handle<Code> LoadStubCompiler::CompileLoadNonexistent(Handle<HeapType> type,
                                                      Handle<JSObject> last,
                                                      Handle<Name> name) {
  NonexistentHandlerFrontend(type, last, name);

  // Return undefined if maps of the full prototype chain are still the
  // same and no global property with this name contains a value.
  __ LoadRoot(r2, Heap::kUndefinedValueRootIndex);
  __ Ret();

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


Register* LoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { r2, r4, r5, r3, r6, r7 };
  return registers;
}


Register* KeyedLoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { r3, r2, r4, r5, r6, r7 };
  return registers;
}


Register StoreStubCompiler::value() {
  return r2;
}


Register* StoreStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3.
  static Register registers[] = { r3, r4, r5, r6, r7 };
  return registers;
}


Register* KeyedStoreStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3.
  static Register registers[] = { r4, r3, r5, r6, r7 };
  return registers;
}


#undef __
#define __ ACCESS_MASM(masm)


void LoadStubCompiler::GenerateLoadViaGetter(MacroAssembler* masm,
                                             Handle<HeapType> type,
                                             Register receiver,
                                             Handle<JSFunction> getter) {
  // ----------- S t a t e -------------
  //  -- r2    : receiver
  //  -- r4    : name
  //  -- lr    : return address
  // -----------------------------------
  {
    FrameAndConstantPoolScope scope(masm, StackFrame::INTERNAL);

    if (!getter.is_null()) {
      // Call the JavaScript getter with the receiver on the stack.
      if (IC::TypeToMap(*type, masm->isolate())->IsJSGlobalObjectMap()) {
        // Swap in the global receiver.
        __ LoadP(receiver,
                 FieldMemOperand(
                   receiver, JSGlobalObject::kGlobalReceiverOffset));
      }
      __ push(receiver);
      ParameterCount actual(0);
      ParameterCount expected(getter);
      __ InvokeFunction(getter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper());
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetGetterStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context register.
    __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> LoadStubCompiler::CompileLoadGlobal(
    Handle<HeapType> type,
    Handle<GlobalObject> global,
    Handle<PropertyCell> cell,
    Handle<Name> name,
    bool is_dont_delete) {
  Label miss;
  HandlerFrontendHeader(type, receiver(), global, name, &miss);

  // Get the value from the cell.
  __ mov(r5, Operand(cell));
  __ LoadP(r6, FieldMemOperand(r5, Cell::kValueOffset));

  // Check for deleted property if property can actually be deleted.
  if (!is_dont_delete) {
    __ CompareRoot(r6, Heap::kTheHoleValueRootIndex);
    __ beq(&miss);
  }

  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->named_load_global_stub(), 1, r3, r5);
  __ LoadRR(r2, r6);
  __ Ret();

  HandlerFrontendFooter(name, &miss);

  // Return the generated code.
  return GetCode(kind(), Code::NORMAL, name);
}


Handle<Code> BaseLoadStoreStubCompiler::CompilePolymorphicIC(
    TypeHandleList* types,
    CodeHandleList* handlers,
    Handle<Name> name,
    Code::StubType type,
    IcCheckType check) {
  Label miss;

  if (check == PROPERTY &&
      (kind() == Code::KEYED_LOAD_IC || kind() == Code::KEYED_STORE_IC)) {
    __ CmpP(this->name(), Operand(name));
    __ bne(&miss);
  }

  Label number_case;
  Label* smi_target = IncludesNumberType(types) ? &number_case : &miss;
  __ JumpIfSmi(receiver(), smi_target);

  Register map_reg = scratch1();

  int receiver_count = types->length();
  int number_of_handled_maps = 0;
  __ LoadP(map_reg, FieldMemOperand(receiver(), HeapObject::kMapOffset));
  for (int current = 0; current < receiver_count; ++current) {
    Handle<HeapType> type = types->at(current);
    Handle<Map> map = IC::TypeToMap(*type, isolate());
    if (!map->is_deprecated()) {
      number_of_handled_maps++;
      __ mov(ip, Operand(map));
      __ CmpP(map_reg, ip);
      if (type->Is(HeapType::Number())) {
        ASSERT(!number_case.is_unused());
        __ bind(&number_case);
      }
      __ Jump(handlers->at(current), RelocInfo::CODE_TARGET, eq);
    }
  }
  ASSERT(number_of_handled_maps != 0);

  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  InlineCacheState state =
      number_of_handled_maps > 1 ? POLYMORPHIC : MONOMORPHIC;
  return GetICCode(kind(), type, name, state);
}


void StoreStubCompiler::GenerateStoreArrayLength() {
  // Prepare tail call to StoreIC_ArrayLength.
  __ Push(receiver(), value());

  ExternalReference ref =
      ExternalReference(IC_Utility(IC::kStoreIC_ArrayLength),
                        masm()->isolate());
  __ TailCallExternalReference(ref, 2, 1);
}


Handle<Code> KeyedStoreStubCompiler::CompileStorePolymorphic(
    MapHandleList* receiver_maps,
    CodeHandleList* handler_stubs,
    MapHandleList* transitioned_maps) {
  Label miss;
  __ JumpIfSmi(receiver(), &miss);

  int receiver_count = receiver_maps->length();
  __ LoadP(scratch1(), FieldMemOperand(receiver(), HeapObject::kMapOffset));
  for (int i = 0; i < receiver_count; ++i) {
    __ CmpP(scratch1(), Operand(receiver_maps->at(i)));
    if (transitioned_maps->at(i).is_null()) {
      Label skip;
      __ bne(&skip);
      __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET);
      __ bind(&skip);
    } else {
      Label next_map;
      __ bne(&next_map, Label::kNear);
      __ mov(transition_map(), Operand(transitioned_maps->at(i)));
      __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET, al);
      __ bind(&next_map);
    }
  }

  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(
      kind(), Code::NORMAL, factory()->empty_string(), POLYMORPHIC);
}


#undef __
#define __ ACCESS_MASM(masm)


void KeyedLoadStubCompiler::GenerateLoadDictionaryElement(
    MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r2     : key
  //  -- r3     : receiver
  // -----------------------------------
  Label slow, miss;

  Register key = r2;
  Register receiver = r3;

  __ UntagAndJumpIfNotSmi(r4, key, &miss);
  __ LoadP(r6, FieldMemOperand(receiver, JSObject::kElementsOffset));
  __ LoadFromNumberDictionary(&slow, r6, key, r2, r4, r5, r7);
  __ Ret();

  __ bind(&slow);
  __ IncrementCounter(
      masm->isolate()->counters()->keyed_load_external_array_slow(),
      1, r4, r5);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r2     : key
  //  -- r3     : receiver
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_Slow);

  // Miss case, call the runtime.
  __ bind(&miss);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r2     : key
  //  -- r3     : receiver
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_Miss);
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_S390
