# BPTR v0 image contract used by the independent tools

This records the existing image encoding consumed by the fixture utilities.
It is separate from the behavioral contract of `bptr_insert`. No insertion
algorithm, split midpoint, or node-allocation order is required by verification.

All integers use the producing host's native byte order, fixed-width integer
types, and no structure padding. Images are not a portable interchange format
between hosts with different byte order. Byte-oriented keys/values have exactly
their configured widths; their interpretation is supplied by the comparator.
The comparator is not serialized in the tree file.

The file contains complete `node_size` blocks. Block 0 is the header. A node
index is its block number, with 0 reserved as the null link. An index occupies
4 bytes in lite mode and 8 bytes in normal mode. Header fields are:

| Offset | Width | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `BPTR` |
| 4 | 4 | version 0, with flag `0x80` for lite mode |
| 8 | 4 | node size |
| 12 | 2 | key width |
| 14 | 2 | value width |
| 16 | 8 | live leaf-record count |
| 24 | 4 | height |
| 28 | pointer | root index |
| 28 + pointer | pointer | free-list head |
| 28 + 2 * pointer | pointer | free-node count |
| 28 + 3 * pointer | pointer | live-node count |

A rootless empty image has height, root, record count and live-node count zero.
Free blocks, if present, must still form a complete free list. The generator
creates a header-only empty image. A nonempty root has no parent and at least
one key. All leaves have level 0; a nonempty root's level is height minus one.

Live-node metadata occupies 64 bytes:

| Offset | Width | Meaning |
| --- | --- | --- |
| 0 | 2 | flags: bit 0 valid, bit 1 leaf |
| 2 | 2 | level |
| 4 | 4 | key count K |
| 8 | 4 | checksum, currently unused |
| 12 | pointer | parent index |
| 12 + pointer | pointer | next sibling |
| 12 + 2 * pointer | pointer | previous sibling |

Key bytes begin at offset 64: K tightly packed fixed-width keys. Immediately
after the **occupied** keys, a leaf stores K fixed-width values; an internal
node stores K+1 child indices. Payload offsets depend on occupied key count,
not capacity. Internal separator i compares equivalent to the minimum leaf key
in child i+1. Sibling chains span each complete level in comparator order.

Leaf capacity L is `floor((node_size - 64) / (key_size + value_size))`.
Branch key capacity B is
`floor((node_size - 64 - pointer) / (key_size + pointer))`.
Non-root leaves require at least `ceil(L/2)` keys. Non-root branches require at
least `floor(B/2)` keys. The root minimum is one. A fixture layout must also
accommodate at least two branch keys and one leaf record. Zero-width records
are not supported by this utility API.

Every live block must be reachable exactly once through root child links.
Child/parent links are reciprocal. The independently derived level order must
match both sibling directions. Every remaining block must be on the free list
exactly once. A free block has valid bit 0 clear and its next-free index at
offset 2; other flag bits and bytes are undefined. Checksums, padding, unused
payload bytes, and unused header bytes are ignored.

The fixture API supports heights 0 through 254. This is an explicit utility
limit, independent of cache capacity. Verification is bounded by the actual
file extent, never by an unchecked header count. It does not invoke library
loading, node decoding, flushing, or unloading.
