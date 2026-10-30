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

// Batch 5 (2026-10-30): 25 loop-free leaf functions of 14-40 instructions.
void Sub_8216F4E8(PPCContext&, uint8_t*);  // sets a flag byte; also +188 when the list is empty
void Sub_821F3320(PPCContext&, uint8_t*);  // self-link and a constant triple
void Sub_821F39B0(PPCContext&, uint8_t*);  // stores f1..f4 into an array element
void Sub_82284F58(PPCContext&, uint8_t*);  // 4x4 matrix from two image constants
void Sub_822EB240(PPCContext&, uint8_t*);  // resets an object from two globals (three words)
void Sub_822FB238(PPCContext&, uint8_t*);  // flag bytes: set 0x21; clear four bits
void Sub_8230A578(PPCContext&, uint8_t*);  // resets an object from two globals
void Sub_82315CE0(PPCContext&, uint8_t*);  // resets an object from a constant at +20
void Sub_823FA9A8(PPCContext&, uint8_t*);  // +588 = global count times a constant
void Sub_82473790(PPCContext&, uint8_t*);  // marks *r3; advances the object; stores a word in its target
void Sub_82479668(PPCContext&, uint8_t*);  // starts a mode on r4 with 100; *r3 = a global
void Sub_825D3808(PPCContext&, uint8_t*);  // profiler block: clears a word and bits; sets mask bits 7 and 44
void Sub_825F6CC8(PPCContext&, uint8_t*);  // advances a parse cursor over an aligned header
void Sub_82626BE8(PPCContext&, uint8_t*);  // listener constructor (secondary vtable A)
void Sub_82694200(PPCContext&, uint8_t*);  // listener constructor (secondary vtable B)
void Sub_827944D8(PPCContext&, uint8_t*);  // constructor: vtable; kind 9
void Sub_82794510(PPCContext&, uint8_t*);  // constructor: vtable; kind 9; five arguments
void Sub_8281B120(PPCContext&, uint8_t*);  // writes one byte of a scaled lookup table
void Sub_828FEE28(PPCContext&, uint8_t*);  // latches pending values
void Sub_82953330(PPCContext&, uint8_t*);  // handle constructor; bumps the target's reference count
void Sub_829EBFF8(PPCContext&, uint8_t*);  // constructor with two vtables (null-tolerant)
void Sub_82A3F0A0(PPCContext&, uint8_t*);  // sets a bounded rate and its inverse
void Sub_82A792E0(PPCContext&, uint8_t*);  // constructor: vtable; 1.0f; cleared fields
void Sub_82B37038(PPCContext&, uint8_t*);  // clears ten global words
void Sub_82B37118(PPCContext&, uint8_t*);  // clears ten global words (second)

// Batch 6 (2026-10-30): 25 loop-free leaf functions of 15-54 instructions.
void Sub_8213E3C8(PPCContext&, uint8_t*);  // twelve words set to a constant
void Sub_82185E40(PPCContext&, uint8_t*);  // distance between two points into *r7; returns 1
void Sub_8218DC98(PPCContext&, uint8_t*);  // accumulates a byte and two floats into a double; counter + 3
void Sub_82213798(PPCContext&, uint8_t*);  // resets an object from a global and a constant
void Sub_822E9A98(PPCContext&, uint8_t*);  // copies three flag bits between bytes
void Sub_822F9FA0(PPCContext&, uint8_t*);  // flag bytes and the owner's byte at +424
void Sub_82303670(PPCContext&, uint8_t*);  // fills r4 from the owner's bytes and halfword
void Sub_823CA8F0(PPCContext&, uint8_t*);  // appends a slot to a bucket list (first table)
void Sub_823CAA10(PPCContext&, uint8_t*);  // appends a slot to a bucket list (second table)
void Sub_8240B648(PPCContext&, uint8_t*);  // accumulates two bytes into a double; counter + 2
void Sub_82476C50(PPCContext&, uint8_t*);  // starts two modes on r4; *r3 = a global
void Sub_82581ED0(PPCContext&, uint8_t*);  // frame counter in state 3; done flag at the definition's count
void Sub_82597840(PPCContext&, uint8_t*);  // resets the target state of r4; *r3 = a global
void Sub_826011F8(PPCContext&, uint8_t*);  // three random floats from the global LCG
void Sub_82652B30(PPCContext&, uint8_t*);  // accumulates a word; two floats and two bytes into a double; counter + 5
void Sub_82726F50(PPCContext&, uint8_t*);  // little-endian float from a byte stream
void Sub_82726FC8(PPCContext&, uint8_t*);  // little-endian word from a byte stream
void Sub_82807698(PPCContext&, uint8_t*);  // stamps the remaining count once the halfword at +202 reaches 512; returns 0
void Sub_8284D670(PPCContext&, uint8_t*);  // two-bit field of a 64-bit big-endian bit array
void Sub_8298FA58(PPCContext&, uint8_t*);  // quad at +12 copied to r4
void Sub_829F0F40(PPCContext&, uint8_t*);  // constructor: vtable; 1.0f pair; 0x80000000
void Sub_82AB52F0(PPCContext&, uint8_t*);  // lower-cases an ASCII letter code
void Sub_82AB5FD0(PPCContext&, uint8_t*);  // floor through fsel
void Sub_82B48FE8(PPCContext&, uint8_t*);  // installs a vtable pointer into a global object
void Sub_82B4A590(PPCContext&, uint8_t*);  // installs a vtable pointer into a global object (second)

// Batch 7 (2026-10-30): 25 loop-free leaf functions of 16-72 instructions.
void Sub_821577C0(PPCContext&, uint8_t*);  // camera target block: flag and a 96-bit value
void Sub_821584F8(PPCContext&, uint8_t*);  // camera pair setter (first)
void Sub_82158588(PPCContext&, uint8_t*);  // camera pair setter (second)
void Sub_82271AD8(PPCContext&, uint8_t*);  // random float in the object's range from a global LCG
void Sub_822F9AD8(PPCContext&, uint8_t*);  // flag bytes: clears six bits
void Sub_822FD2E8(PPCContext&, uint8_t*);  // flag bytes: sets two; clears four
void Sub_8230E280(PPCContext&, uint8_t*);  // flag bytes: sets two; keeps 0x3E; clears two
void Sub_8232EFB0(PPCContext&, uint8_t*);  // render state write into the global shadow and dirty mask
void Sub_823CD950(PPCContext&, uint8_t*);  // accumulates a list length into a double; counter + 1
void Sub_8240B7B8(PPCContext&, uint8_t*);  // accumulates two bytes and two floats into a double; counter + 4
void Sub_82474CE0(PPCContext&, uint8_t*);  // copies a mode into a record; *r5 = 1; *r3 = the mode global
void Sub_82477E88(PPCContext&, uint8_t*);  // starts two modes on r4 with 200; *r3 = the mode global
void Sub_8250ADE0(PPCContext&, uint8_t*);  // invalidates seven cached handles of a global
void Sub_825111A8(PPCContext&, uint8_t*);  // accumulates a global LCG draw into a double; counter + 1
void Sub_826D5C28(PPCContext&, uint8_t*);  // constructor of a 208-byte render state
void Sub_82730D90(PPCContext&, uint8_t*);  // constructor of a 48-byte node
void Sub_827C7F08(PPCContext&, uint8_t*);  // resets a stream's limits and flags; returns 0
void Sub_8283EEC8(PPCContext&, uint8_t*);  // priority of an object (explicit or 2046 / 2047)
void Sub_82846518(PPCContext&, uint8_t*);  // packs four 2-bit fields selected by a selector
void Sub_8297DEB0(PPCContext&, uint8_t*);  // pops a halfword pair from a small stack into a slot
void Sub_8298D7B0(PPCContext&, uint8_t*);  // constructor (null-tolerant); 1.0f replaced by a constant
void Sub_82A24A70(PPCContext&, uint8_t*);  // distance between two points into *r7; returns 1
void Sub_82AB52B0(PPCContext&, uint8_t*);  // upper-cases an ASCII letter code
void Sub_82B4A610(PPCContext&, uint8_t*);  // installs a vtable pointer into a global object (third)
void Sub_82B4A7A0(PPCContext&, uint8_t*);  // installs a vtable pointer into a global object (fourth)

// Batch 8 (2026-10-30): 25 loop-free leaf functions of 17-61 instructions.
void Sub_82157A18(PPCContext&, uint8_t*);  // camera target block (second): flag and a 96-bit value
void Sub_821584B0(PPCContext&, uint8_t*);  // camera pair setter (third)
void Sub_82158540(PPCContext&, uint8_t*);  // camera pair setter (fourth)
void Sub_82213708(PPCContext&, uint8_t*);  // constructor of a 52-byte listener with an embedded node
void Sub_822137E0(PPCContext&, uint8_t*);  // f1 = sum of the listener's fields as floats
void Sub_822FC1A8(PPCContext&, uint8_t*);  // flag bytes; more cleared when the object's +37 is clear
void Sub_82314E50(PPCContext&, uint8_t*);  // flag bytes: clears five; sets 0x30
void Sub_82316328(PPCContext&, uint8_t*);  // accumulates a signed byte and a byte into a double; counter + 2
void Sub_823D19A8(PPCContext&, uint8_t*);  // profiler block: a quad copied; mask bit 63 set
void Sub_8240B8D0(PPCContext&, uint8_t*);  // accumulates three bytes and two floats into a double; counter + 5
void Sub_82474118(PPCContext&, uint8_t*);  // starts three modes on r4; *r3 = the mode global
void Sub_824C55E8(PPCContext&, uint8_t*);  // f1 = sum of nine floats and a constant
void Sub_82581CF8(PPCContext&, uint8_t*);  // constructor of a 72-byte settings block
void Sub_8259FD50(PPCContext&, uint8_t*);  // stores four 64-bit arguments
void Sub_825FBDB8(PPCContext&, uint8_t*);  // copies 18 words with two replaced by the pad constant
void Sub_82740710(PPCContext&, uint8_t*);  // default stream parameters
void Sub_82794E68(PPCContext&, uint8_t*);  // constructor: vtable; kind 14; 3 at +16
void Sub_82832BF8(PPCContext&, uint8_t*);  // reads an n-bit field of a little-endian bit stream
void Sub_8285E988(PPCContext&, uint8_t*);  // permutes four 2-bit fields by a selector
void Sub_8294DDD0(PPCContext&, uint8_t*);  // constructor: vtable; 1; 0x7FFFFFFF
void Sub_829DC450(PPCContext&, uint8_t*);  // constructor (null-tolerant): eight words from a constant
void Sub_82A4BF98(PPCContext&, uint8_t*);  // constructor of a 24-byte proxy
void Sub_82A97790(PPCContext&, uint8_t*);  // min(r4; (k1 / f1) * k2 + half) truncated
void Sub_82B0FEC0(PPCContext&, uint8_t*);  // default colour set from two constants (third)
void Sub_82B52010(PPCContext&, uint8_t*);  // registers a global object through its RTTI offset

}  // namespace crackdown::hooks::decomp
