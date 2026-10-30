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

// Batch 3 (2026-10-30): 25 small loop-free leaf functions.
void Sub_8213F180(PPCContext&, uint8_t*);  // copies the object's float triple at +304 as a quad
void Sub_821BFCC0(PPCContext&, uint8_t*);  // triple at +12 of element r5 of the array at +84
void Sub_821F58D0(PPCContext&, uint8_t*);  // copies the float triple at +288 as a quad
void Sub_82258550(PPCContext&, uint8_t*);  // toggles a global flag byte; clears a global word when it turns off
void Sub_82270B80(PPCContext&, uint8_t*);  // links r4 to the object when the id matches and the flag at +556 is clear
void Sub_822EBBB0(PPCContext&, uint8_t*);  // resets an object from two globals
void Sub_82311448(PPCContext&, uint8_t*);  // sets state 4 when the owner's value is above a threshold
void Sub_82326300(PPCContext&, uint8_t*);  // resets a counter at the object's limit; reports it in *r5
void Sub_82369348(PPCContext&, uint8_t*);  // f1 = count * constant
void Sub_824879B8(PPCContext&, uint8_t*);  // copies the triple at r5 as a quad
void Sub_824888C8(PPCContext&, uint8_t*);  // copies the float triple at +800 as a quad
void Sub_825065E0(PPCContext&, uint8_t*);  // copies the quad at +400
void Sub_8253C118(PPCContext&, uint8_t*);  // ordering of two objects by the key at +68 (-1 / 1)
void Sub_8265CDC8(PPCContext&, uint8_t*);  // timer tick: decrements and disarms at zero
void Sub_8269B3C8(PPCContext&, uint8_t*);  // constructor: kind 20 and an argument
void Sub_8273B2F8(PPCContext&, uint8_t*);  // constructor: five 1.0f and a count of 3
void Sub_82793BB8(PPCContext&, uint8_t*);  // constructor: vtable, kind 2, four arguments
void Sub_8281BDD0(PPCContext&, uint8_t*);  // reports a format (9, 5, two image tables); returns 0
void Sub_828D2700(PPCContext&, uint8_t*);  // node constructor linked back from its owner
void Sub_82993038(PPCContext&, uint8_t*);  // sets a byte and the target's pair to 1.0f
void Sub_829F1AD8(PPCContext&, uint8_t*);  // packs four bytes into a word
void Sub_82A85C38(PPCContext&, uint8_t*);  // stores f1 at +40 of r4 and sets flag bit 0
void Sub_82A89BE0(PPCContext&, uint8_t*);  // clears a count and an array slot; returns 0
void Sub_82B1F4A8(PPCContext&, uint8_t*);  // default colour triple from two constants
void Sub_82B1F4D0(PPCContext&, uint8_t*);  // default colour triple from two constants (second)

// Batch 4 (2026-10-30): 25 loop-free leaf functions of 13-39 instructions.
void Sub_821681D0(PPCContext&, uint8_t*);  // triple i of a 16-byte array as a quad
void Sub_821C5D38(PPCContext&, uint8_t*);  // triple at +12 of a nested table element
void Sub_821C5D70(PPCContext&, uint8_t*);  // triple at +64 of a nested table element
void Sub_82202068(PPCContext&, uint8_t*);  // masked flags of a table entry differ from a global
void Sub_822ECBC0(PPCContext&, uint8_t*);  // flag bytes: set 0x30 / clear 0x20; 0x28; 0x30
void Sub_822FE9F0(PPCContext&, uint8_t*);  // resets an object from a global; mode 3
void Sub_82305158(PPCContext&, uint8_t*);  // state 4 when the owner has bit 2 and its target lacks bit 7
void Sub_8230EE50(PPCContext&, uint8_t*);  // flag bytes: set 0x20 twice; keep 0x3E; clear 0x24
void Sub_82315E00(PPCContext&, uint8_t*);  // resets an object from a constant
void Sub_824748B0(PPCContext&, uint8_t*);  // starts mode 2 on r4; *r3 = a global
void Sub_82474CA8(PPCContext&, uint8_t*);  // starts mode 1 on r4 with 1000; *r3 = a global
void Sub_8258AEA8(PPCContext&, uint8_t*);  // resets a timer from its definition
void Sub_825BFA18(PPCContext&, uint8_t*);  // caches two words indexed by a global counter
void Sub_8261BCF8(PPCContext&, uint8_t*);  // viewport from the device: four truncated floats and two words; returns 0
void Sub_8264FAA0(PPCContext&, uint8_t*);  // accumulates four fields into a double; counter + 4
void Sub_82793ED8(PPCContext&, uint8_t*);  // constructor: vtable; kind 4; four arguments
void Sub_827F4660(PPCContext&, uint8_t*);  // stamps the remaining count into the current slot; returns 0
void Sub_82832F88(PPCContext&, uint8_t*);  // stores eight arguments into a global block
void Sub_8287A968(PPCContext&, uint8_t*);  // sort key difference of two entries (64-bit)
void Sub_8293D600(PPCContext&, uint8_t*);  // clears the text builder
void Sub_829518F0(PPCContext&, uint8_t*);  // constructor: "Unknown Heading"
void Sub_82AC4470(PPCContext&, uint8_t*);  // adds r4 to the exponent of f1
void Sub_82AEDC80(PPCContext&, uint8_t*);  // range of the current entry of a global block
void Sub_82B0FDA8(PPCContext&, uint8_t*);  // default colour set from two constants
void Sub_82B0FE18(PPCContext&, uint8_t*);  // default colour set from two constants (second)

}  // namespace crackdown::hooks::decomp
