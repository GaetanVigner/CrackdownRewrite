#pragma once

// Native translations of the title's own functions (Any360 stage 2, docs/PIPELINE.md of the Any360 repository):
// each replaces one recompiled leaf function outright, with the same register ABI (arguments in r3..., result in r3),
// and is proven equal to the recompiled original on random inputs by tests/decomp_io_tests. Grouped by batch; the
// bindings are in guest_bindings_base.cpp.

#include <cstdint>

#include <rex/ppc.h>

namespace crackdown::hooks::decomp {

// Batch 1 (2026-10-30): 13 leaf functions of the game code, picked for variety (integer, float, pointer walks).
void Sub_8213BC38(PPCContext&, uint8_t*);  // first chunk tagged 0x40000000 of a chunk list: four words, halfword-swapped
void Sub_82144D80(PPCContext&, uint8_t*);  // entity visible to the local player (owner or flag bit test)
void Sub_8216F470(PPCContext&, uint8_t*);  // clears two fields of every object in an embedded pointer array
void Sub_821CAA40(PPCContext&, uint8_t*);  // address of an indexed element through a table of (index, owner) pairs
void Sub_821E72D0(PPCContext&, uint8_t*);  // whether a value is one of two cached pointers of an object
void Sub_821E8E90(PPCContext&, uint8_t*);  // string equal to a fixed name of the image
void Sub_822B7408(PPCContext&, uint8_t*);  // character state flags test (ragdoll / vehicle bits)
void Sub_823E9BD0(PPCContext&, uint8_t*);  // orb map view: pan and zoom parameters from a scale
void Sub_82794868(PPCContext&, uint8_t*);  // constructor: vtable, kind 11, 15 zero fields
void Sub_8294AA18(PPCContext&, uint8_t*);  // profiler slot: record start time, mark the slot's bit in a 64-bit mask
void Sub_82981388(PPCContext&, uint8_t*);  // largest packed size over an array of entries
void Sub_82A94078(PPCContext&, uint8_t*);  // index where the running sum of an array reaches a fraction of its total (min 76)
void Sub_82B47D50(PPCContext&, uint8_t*);  // three float pairs of a global from the image's 0.0 / 1.0 constants

// Batch 2 (2026-10-30): 11 larger leaf functions (40-130 instructions).
void Sub_82340008(PPCContext&, uint8_t*);  // relocates a loaded table's two entry tables
void Sub_823925C0(PPCContext&, uint8_t*);  // name lookup in the global hash table, checked against the caller's key
void Sub_823B7838(PPCContext&, uint8_t*);  // ORs an object's masks into the global render state masks
void Sub_82463160(PPCContext&, uint8_t*);  // constructor of a 264-byte state object
void Sub_8246DAE0(PPCContext&, uint8_t*);  // ring buffer push, returns the write index
void Sub_824ABED0(PPCContext&, uint8_t*);  // blend of two curves into the object's curve with weight f1
void Sub_824DA608(PPCContext&, uint8_t*);  // clears a page slot and records the item in the manager's list
void Sub_825FF080(PPCContext&, uint8_t*);  // copies 80-byte elements with two words replaced by a constant
void Sub_8272F260(PPCContext&, uint8_t*);  // constructor of a 192-byte object
void Sub_82944E58(PPCContext&, uint8_t*);  // appends glyph codes for the current symbol to a string
void Sub_82A95780(PPCContext&, uint8_t*);  // weighted pick between two values (f1..f4 -> f1)

}  // namespace crackdown::hooks::decomp
