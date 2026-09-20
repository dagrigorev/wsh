/* Ported from Ghostty src/terminal/page.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Cell and Row, the storage primitives a Page is built from.
 *
 * PARTIAL PORT. Cell, Row, Capacity, the layout, and the Page struct's
 * storage layer are here, along with the definitions that close the style and
 * hyperlink cycles. The larger operations — resize, clone, scrolling, grapheme
 * management — are still to come, so the ledger records page.zig as `wip`.
 *
 * BIT LAYOUT. Both Cell and Row are exactly 64 bits, and the field order
 * matters: it is what lets a row of cells be memcpy'd and a page be relocated
 * wholesale. Zig packs a packed struct from the least significant bit upward,
 * so the field order below matches the declaration order upstream.
 *
 *   Cell                                Row
 *     0..1   content_tag                  0..31  cells (Offset)
 *     2..25  content (union)             32      wrap
 *    26..41  style_id                    33      wrap_continuation
 *    42..43  wide                        34      grapheme
 *    44      protected                   35      styled
 *    45      hyperlink                   36      hyperlink
 *    46..47  semantic_content            37..38  semantic_prompt
 *    48..63  padding                     39      kitty_virtual_placeholder
 *                                        40      dirty
 *                                        41..63  padding
 *
 * This is written as explicit shifts over a uint64_t rather than as C++
 * bitfields. Bitfield allocation order is implementation-defined, so a
 * bitfield version would be a layout that happens to work on one compiler
 * rather than the layout being reproduced.
 */

#pragma once
#ifndef WISP_TERMINAL_PAGE_HPP
#define WISP_TERMINAL_PAGE_HPP

#include <stddef.h>
#include <stdint.h>

#include "size.hpp"
#include "color.hpp"
#include "style.hpp"
#include "bitmap_allocator.hpp"
#include "hash_map.hpp"
#include "hyperlink.hpp"

namespace wisp {
namespace terminal {

/* ─── cell enums ─────────────────────────────────────────────────────────── */

/* Selects which member of a cell's content union is active, and affects some
 * behavior besides. */
enum class ContentTag : uint8_t {
    /* A single codepoint, possibly zero for an empty cell. */
    codepoint = 0,

    /* A codepoint that begins a multi-codepoint grapheme cluster. The
     * codepoint member is still active, but more codepoints live in the
     * page's grapheme data. */
    codepoint_grapheme = 1,

    /* No text, only a background color. Keeping these out of the style map
     * saves both a style slot and a lookup, which matters because large runs
     * of background-only cells are common. */
    bg_color_palette = 2,
    bg_color_rgb = 3,
};

/* A grid cell is one or two columns wide, and a wide character is always
 * followed by a spacer. This encodes both the width and the spacer role. */
enum class Wide : uint8_t {
    narrow = 0,
    wide = 1,

    /* Follows a wide character; not rendered. */
    spacer_tail = 2,

    /* Sits at the end of a soft-wrapped line to show that a wide character
     * continues on the next one. */
    spacer_head = 3,
};

/* Semantic type of a cell's content, from OSC 133. */
enum class SemanticContent : uint8_t {
    output = 0,
    input = 1,
    prompt = 2,
};

/* Whether a row holds prompt cells, and which kind. */
enum class SemanticPrompt : uint8_t {
    none = 0,

    /* A primary prompt line: the start of a prompt, not a continuation. */
    prompt = 1,

    /* A continuation line, marked with k=c. Used to decide that a line
     * belongs to some earlier prompt. */
    prompt_continuation = 2,
};

/* ─── Cell ───────────────────────────────────────────────────────────────── */

struct Cell {
    uint64_t bits;

    Cell() : bits(0) {}

    /* Field positions. Kept as named constants because the tests assert
     * against them, so a field that moves fails loudly rather than quietly
     * reinterpreting existing cells. */
    static const int CONTENT_TAG_SHIFT = 0;
    static const int CONTENT_SHIFT = 2;
    static const int STYLE_ID_SHIFT = 26;
    static const int WIDE_SHIFT = 42;
    static const int PROTECTED_SHIFT = 44;
    static const int HYPERLINK_SHIFT = 45;
    static const int SEMANTIC_SHIFT = 46;

    static const uint64_t CONTENT_TAG_MASK = 0x3;
    static const uint64_t CONTENT_MASK = 0xFFFFFF;      /* 24 bits */
    static const uint64_t STYLE_ID_MASK = 0xFFFF;
    static const uint64_t WIDE_MASK = 0x3;
    static const uint64_t SEMANTIC_MASK = 0x3;

    /* Within the 24-bit content union. */
    static const uint32_t CODEPOINT_MASK = 0x1FFFFF;    /* 21 bits */
    static const uint32_t PALETTE_MASK = 0xFF;

    ContentTag content_tag() const {
        return (ContentTag)((bits >> CONTENT_TAG_SHIFT) & CONTENT_TAG_MASK);
    }
    void set_content_tag(ContentTag t) {
        bits = (bits & ~(CONTENT_TAG_MASK << CONTENT_TAG_SHIFT)) |
               (((uint64_t)t & CONTENT_TAG_MASK) << CONTENT_TAG_SHIFT);
    }

    uint32_t content_raw() const {
        return (uint32_t)((bits >> CONTENT_SHIFT) & CONTENT_MASK);
    }
    void set_content_raw(uint32_t v) {
        bits = (bits & ~(CONTENT_MASK << CONTENT_SHIFT)) |
               (((uint64_t)v & CONTENT_MASK) << CONTENT_SHIFT);
    }

    /* The codepoint, valid when the tag is codepoint or codepoint_grapheme. */
    uint32_t codepoint() const { return content_raw() & CODEPOINT_MASK; }
    void set_codepoint(uint32_t cp) { set_content_raw(cp & CODEPOINT_MASK); }

    /* The palette index, valid when the tag is bg_color_palette. */
    uint8_t color_palette() const { return (uint8_t)(content_raw() & PALETTE_MASK); }
    void set_color_palette(uint8_t idx) { set_content_raw(idx); }

    /* The background color, valid when the tag is bg_color_rgb. */
    RGB color_rgb() const {
        const uint32_t c = content_raw();
        return RGB((uint8_t)(c & 0xFF),
                   (uint8_t)((c >> 8) & 0xFF),
                   (uint8_t)((c >> 16) & 0xFF));
    }
    void set_color_rgb(RGB v) {
        set_content_raw((uint32_t)v.r | ((uint32_t)v.g << 8) | ((uint32_t)v.b << 16));
    }

    /* Index into the page's style set. Zero is the default style, which needs
     * no lookup. */
    style::Id style_id() const {
        return (style::Id)((bits >> STYLE_ID_SHIFT) & STYLE_ID_MASK);
    }
    void set_style_id(style::Id id) {
        bits = (bits & ~(STYLE_ID_MASK << STYLE_ID_SHIFT)) |
               (((uint64_t)id & STYLE_ID_MASK) << STYLE_ID_SHIFT);
    }

    Wide wide() const { return (Wide)((bits >> WIDE_SHIFT) & WIDE_MASK); }
    void set_wide(Wide w) {
        bits = (bits & ~(WIDE_MASK << WIDE_SHIFT)) |
               (((uint64_t)w & WIDE_MASK) << WIDE_SHIFT);
    }

    bool protect() const { return ((bits >> PROTECTED_SHIFT) & 1) != 0; }
    void set_protect(bool v) {
        bits = (bits & ~((uint64_t)1 << PROTECTED_SHIFT)) |
               ((uint64_t)(v ? 1 : 0) << PROTECTED_SHIFT);
    }

    /* Whether this cell belongs to a hyperlink. The ID lives in the page's
     * hyperlink map rather than in the cell. */
    bool hyperlink() const { return ((bits >> HYPERLINK_SHIFT) & 1) != 0; }
    void set_hyperlink(bool v) {
        bits = (bits & ~((uint64_t)1 << HYPERLINK_SHIFT)) |
               ((uint64_t)(v ? 1 : 0) << HYPERLINK_SHIFT);
    }

    SemanticContent semantic_content() const {
        return (SemanticContent)((bits >> SEMANTIC_SHIFT) & SEMANTIC_MASK);
    }
    void set_semantic_content(SemanticContent s) {
        bits = (bits & ~(SEMANTIC_MASK << SEMANTIC_SHIFT)) |
               (((uint64_t)s & SEMANTIC_MASK) << SEMANTIC_SHIFT);
    }

    /* ─── derived ────────────────────────────────────────────────────────── */

    /* True if the cell holds no text. A background-only cell still counts as
     * empty of text. */
    bool has_text() const {
        const ContentTag t = content_tag();
        return (t == ContentTag::codepoint || t == ContentTag::codepoint_grapheme) &&
               codepoint() != 0;
    }

    /* True if the cell participates in a multi-codepoint grapheme cluster. */
    bool has_grapheme() const {
        return content_tag() == ContentTag::codepoint_grapheme;
    }

    /* True if the cell carries a background color of its own, which takes
     * precedence over its style's background. */
    bool has_bg_color() const {
        const ContentTag t = content_tag();
        return t == ContentTag::bg_color_palette || t == ContentTag::bg_color_rgb;
    }

    bool eql(const Cell &o) const { return bits == o.bits; }
};

/* ─── Row ────────────────────────────────────────────────────────────────── */

struct Row {
    uint64_t bits;

    Row() : bits(0) {}

    static const int CELLS_SHIFT = 0;
    static const int WRAP_SHIFT = 32;
    static const int WRAP_CONT_SHIFT = 33;
    static const int GRAPHEME_SHIFT = 34;
    static const int STYLED_SHIFT = 35;
    static const int HYPERLINK_SHIFT = 36;
    static const int SEMANTIC_PROMPT_SHIFT = 37;
    static const int KITTY_PLACEHOLDER_SHIFT = 39;
    static const int DIRTY_SHIFT = 40;

    static const uint64_t CELLS_MASK = 0xFFFFFFFF;
    static const uint64_t SEMANTIC_PROMPT_MASK = 0x3;

    /* The row's cells, as an offset from the page base. */
    Offset<Cell> cells() const {
        Offset<Cell> o;
        o.offset = (OffsetInt)((bits >> CELLS_SHIFT) & CELLS_MASK);
        return o;
    }
    void set_cells(Offset<Cell> o) {
        bits = (bits & ~(CELLS_MASK << CELLS_SHIFT)) |
               (((uint64_t)o.offset & CELLS_MASK) << CELLS_SHIFT);
    }

    /* Soft-wrapped: the next row continues this one. */
    bool wrap() const { return bit(WRAP_SHIFT); }
    void set_wrap(bool v) { set_bit(WRAP_SHIFT, v); }

    /* This row continues the previous one. */
    bool wrap_continuation() const { return bit(WRAP_CONT_SHIFT); }
    void set_wrap_continuation(bool v) { set_bit(WRAP_CONT_SHIFT, v); }

    /* Some cell here has a multi-codepoint grapheme cluster, so the fast
     * paths that skip grapheme cleanup are unavailable. */
    bool grapheme() const { return bit(GRAPHEME_SHIFT); }
    void set_grapheme(bool v) { set_bit(GRAPHEME_SHIFT, v); }

    /* Some cell here uses a ref-counted style.
     *
     * False positives are allowed, false negatives are not: it is set the
     * first time a style is used and never cleared, because checking whether
     * a style is still in use would cost more than it saves. Erase operations
     * use it to skip style cleanup entirely on rows that were never styled,
     * which upstream measures at roughly 4x. */
    bool styled() const { return bit(STYLED_SHIFT); }
    void set_styled(bool v) { set_bit(STYLED_SHIFT, v); }

    /* Some cell here is part of a hyperlink. Same false-positive rule as
     * styled. */
    bool hyperlink() const { return bit(HYPERLINK_SHIFT); }
    void set_hyperlink(bool v) { set_bit(HYPERLINK_SHIFT, v); }

    /* Whether this row holds prompt cells. Only an optimization for
     * jump-to-prompt; individual cells still have to be checked. False
     * positives are allowed, false negatives are not. */
    SemanticPrompt semantic_prompt() const {
        return (SemanticPrompt)((bits >> SEMANTIC_PROMPT_SHIFT) & SEMANTIC_PROMPT_MASK);
    }
    void set_semantic_prompt(SemanticPrompt p) {
        bits = (bits & ~(SEMANTIC_PROMPT_MASK << SEMANTIC_PROMPT_SHIFT)) |
               (((uint64_t)p & SEMANTIC_PROMPT_MASK) << SEMANTIC_PROMPT_SHIFT);
    }

    /* Holds a Kitty graphics virtual placeholder (U+10EEEE). The bit is kept
     * even when Kitty graphics are disabled so the layout does not change. */
    bool kitty_virtual_placeholder() const { return bit(KITTY_PLACEHOLDER_SHIFT); }
    void set_kitty_virtual_placeholder(bool v) { set_bit(KITTY_PLACEHOLDER_SHIFT, v); }

    /* Needs redrawing. Set by anything that changes the row's contents or
     * position, and cleared by whoever draws it.
     *
     * Conveys only that something changed visually. False positives are
     * allowed; a false negative leaves an artifact on screen. */
    bool dirty() const { return bit(DIRTY_SHIFT); }
    void set_dirty(bool v) { set_bit(DIRTY_SHIFT, v); }

private:
    bool bit(int shift) const { return ((bits >> shift) & 1) != 0; }
    void set_bit(int shift, bool v) {
        bits = (bits & ~((uint64_t)1 << shift)) | ((uint64_t)(v ? 1 : 0) << shift);
    }
};

/* ─── page storage ───────────────────────────────────────────────────────── */

/* The cell array starts on a cache line so that a row's cells never begin
 * mid-line and straddle an extra one. */
static const size_t CELLS_ALIGN =
    alignof(Cell) > 64 ? alignof(Cell) : 64;

/* Grapheme codepoints past the first are stored as u32s, so a 4-byte chunk
 * wastes nothing. */
typedef BitmapAllocator<4> GraphemeAlloc;

/* Byte storage for hyperlink URIs and other page strings. */
typedef BitmapAllocator<8> StringAlloc;

/* Where a cell's extra grapheme codepoints live within grapheme_alloc. */
struct GraphemeSlice {
    OffsetInt offset;
    uint32_t  len;

    GraphemeSlice() : offset(0), len(0) {}
    bool operator==(const GraphemeSlice &o) const {
        return offset == o.offset && len == o.len;
    }
};

/* Maps a cell — keyed by its offset from the page base — to its grapheme
 * data. Keyed by raw offset rather than Offset<Cell> because the key only
 * needs to compare equal, and a plain integer does that without dragging
 * operator== onto Offset. */
typedef OffsetHashMap<OffsetInt, GraphemeSlice> GraphemeMap;

/* Default sizes for the variable-length regions. Each is a starting point
 * that a page grows past by being reallocated at a larger capacity, not a
 * hard ceiling on what a terminal can display. */
static const GraphemeBytesInt GRAPHEME_BYTES_DEFAULT = 512;
static const StringBytesInt   STRING_BYTES_DEFAULT = 512;
static const HyperlinkCountInt HYPERLINK_BYTES_DEFAULT = 512;

/* How much of a page is given over to each thing. */
struct Capacity {
    CellCountInt cols;
    CellCountInt rows;

    /* Distinct styles usable on this page. */
    StyleCountInt styles;

    /* Rough byte budget for hyperlinks. Actual usage runs higher, since
     * hyperlinks also consume string bytes and lookup metadata. */
    HyperlinkCountInt hyperlink_bytes;

    GraphemeBytesInt grapheme_bytes;
    StringBytesInt   string_bytes;

    Capacity()
        : cols(0), rows(0), styles(16),
          hyperlink_bytes(HYPERLINK_BYTES_DEFAULT),
          grapheme_bytes(GRAPHEME_BYTES_DEFAULT),
          string_bytes(STRING_BYTES_DEFAULT) {}

    Capacity(CellCountInt c, CellCountInt r) : Capacity() { cols = c; rows = r; }
};

/* The memory layout of a page.
 *
 * Laid out as row headers, then the cell array, then the metadata block:
 *
 *   [rows][cells][styles, graphemes, strings]
 *
 * Row headers start at offset zero and the cell array is cache-line aligned.
 * Only the row headers need initializing: cells and every metadata member
 * treat all-zero as their empty state, so the pages behind everything past
 * the row headers stay untouched until first use.
 *
 * The hyperlink regions sit at the end. A cell that is part of a link carries
 * only a bit; the map turns that cell's offset into a link ID, and the set
 * holds the link data itself so a run of cells under one OSC 8 sequence
 * shares one entry.
 */
struct PageLayout {
    size_t total_size;

    size_t rows_start;
    size_t rows_size;

    size_t cells_start;
    size_t cells_size;

    size_t              styles_start;
    style::Set::Layout  styles_layout;

    size_t                 grapheme_alloc_start;
    GraphemeAlloc::Layout  grapheme_alloc_layout;

    size_t             grapheme_map_start;
    GraphemeMap::Layout grapheme_map_layout;

    size_t              string_alloc_start;
    StringAlloc::Layout string_alloc_layout;

    size_t                 hyperlink_map_start;
    hyperlink::Map::Layout hyperlink_map_layout;

    size_t                 hyperlink_set_start;
    hyperlink::Set::Layout hyperlink_set_layout;

    Capacity capacity;

    static PageLayout init(const Capacity &cap) {
        PageLayout l;
        l.capacity = cap;

        l.rows_start = 0;
        l.rows_size = (size_t)cap.rows * sizeof(Row);
        const size_t rows_end = l.rows_start + l.rows_size;

        l.cells_start = align_forward(rows_end, CELLS_ALIGN);
        l.cells_size = (size_t)cap.rows * (size_t)cap.cols * sizeof(Cell);
        const size_t cells_end = l.cells_start + l.cells_size;

        /* The metadata block. Each member publishes its own layout, and each
         * starts aligned for what it holds. */
        l.styles_layout = style::Set::Layout::init(
            style::Set::capacity_for_count(cap.styles));
        l.styles_start = align_forward(cells_end, style::Set::base_align);
        const size_t styles_end = l.styles_start + l.styles_layout.total_size;

        l.grapheme_alloc_layout = GraphemeAlloc::layout(cap.grapheme_bytes);
        l.grapheme_alloc_start = align_forward(styles_end, GraphemeAlloc::base_align);
        const size_t grapheme_alloc_end =
            l.grapheme_alloc_start + l.grapheme_alloc_layout.total_size;

        /* One grapheme map entry per cell that could carry graphemes, bounded
         * by what the grapheme bytes could actually describe. */
        l.grapheme_map_layout = GraphemeMap::Layout::init(
            GraphemeMap::capacity_for_count(cap.grapheme_bytes / 4));
        l.grapheme_map_start = align_forward(grapheme_alloc_end, alignof(uint64_t));
        const size_t grapheme_map_end =
            l.grapheme_map_start + l.grapheme_map_layout.total_size;

        l.string_alloc_layout = StringAlloc::layout(cap.string_bytes);
        l.string_alloc_start = align_forward(grapheme_map_end, StringAlloc::base_align);
        const size_t string_alloc_end =
            l.string_alloc_start + l.string_alloc_layout.total_size;

        /* Many cells can share one link, so the map scales with cells while
         * the set scales with the byte budget. Sizing the map to every cell
         * would roughly double a page for something almost no cell uses, so
         * it is bounded at an eighth of them with a floor — a page with more
         * distinct linked cells than that refuses the extra links rather
         * than growing, and the text still renders. */
        {
            const size_t cells = (size_t)cap.rows * (size_t)cap.cols;
            size_t map_entries = cells / 8;
            if (map_entries < 64) map_entries = 64;

            l.hyperlink_map_layout = hyperlink::Map::Layout::init(
                hyperlink::Map::capacity_for_count((uint32_t)map_entries));
            l.hyperlink_map_start = align_forward(string_alloc_end, alignof(uint64_t));
            const size_t hyperlink_map_end =
                l.hyperlink_map_start + l.hyperlink_map_layout.total_size;

            /* A link needs a URI, so the byte budget bounds how many can
             * exist. Sixteen bytes is a short but plausible URI. */
            size_t set_entries = (size_t)cap.hyperlink_bytes / 16;
            if (set_entries < 8) set_entries = 8;

            l.hyperlink_set_layout = hyperlink::Set::Layout::init(
                hyperlink::Set::capacity_for_count(set_entries));
            l.hyperlink_set_start =
                align_forward(hyperlink_map_end, hyperlink::Set::base_align);

            l.total_size = l.hyperlink_set_start + l.hyperlink_set_layout.total_size;
        }

        return l;
    }

    /* Bytes the grid occupies — the row headers plus the cell array. Used
     * when refitting a capacity into a fixed allocation. */
    static size_t grid_bytes(const Capacity &cap) {
        const PageLayout l = init(cap);
        return l.rows_size + l.cells_size;
    }
};

/* The widest grid that fits this capacity without growing it, or 0 if not
 * even one column fits.
 *
 * A single row's header takes a whole cell-aligned region ahead of the cells,
 * so that comes off the top before dividing what is left by the cell size. */
inline CellCountInt capacity_max_cols(const Capacity &cap) {
    const PageLayout l = PageLayout::init(cap);
    const size_t grid = l.rows_size + l.cells_size;

    const size_t row_region = align_forward(sizeof(Row), CELLS_ALIGN);
    if (grid <= row_region) return 0;

    const size_t max_cols = (grid - row_region) / sizeof(Cell);
    const size_t clamp = (size_t)(CellCountInt)-1;
    return (CellCountInt)(max_cols < clamp ? max_cols : clamp);
}

/* Refit a capacity to a new column count without growing the allocation.
 *
 * Only the row count gives; everything else may grow. Because the cell array
 * is cache-line aligned, the padding between the row headers and the cells
 * depends on the row count, so rows are trimmed until the layout fits. That
 * padding is under one cache line, so this settles in a few iterations.
 *
 * Returns false if no row count fits. */
inline bool capacity_adjust_cols(const Capacity &in, CellCountInt cols, Capacity *out) {
    const size_t total_size = PageLayout::init(in).total_size;

    const PageLayout l = PageLayout::init(in);
    const size_t grid = l.rows_size + l.cells_size;

    const size_t bytes_per_row = sizeof(Row) + sizeof(Cell) * (size_t)cols;
    if (bytes_per_row == 0) return false;

    Capacity adjusted = in;
    adjusted.cols = cols;

    size_t new_rows = grid / bytes_per_row;
    while (new_rows > 0) {
        adjusted.rows = (CellCountInt)new_rows;
        if (PageLayout::init(adjusted).total_size <= total_size) {
            *out = adjusted;
            return true;
        }
        new_rows--;
    }

    return false;
}

/* ─── Page ───────────────────────────────────────────────────────────────── */

/* A self-contained section of terminal screen.
 *
 * Everything a page needs lives in one allocation: the row headers, the cell
 * array, the interned style set, and the grapheme and string storage. Nothing
 * inside holds an absolute pointer — every internal reference is a byte offset
 * from the allocation base — so a page can be memcpy'd to a different address,
 * written to disk, or moved between allocations and still work. That is what
 * `relocate` demonstrates, and it is the reason for the offset machinery
 * underneath all of this.
 *
 * PARTIAL. This is the storage layer: construction, addressing cells and rows,
 * and style assignment with its reference counting. The larger operations from
 * upstream's page.zig — resize, clone, scrolling, grapheme and hyperlink
 * management — are not here yet.
 */
struct Page {
    /* Base of the backing allocation. The only absolute pointer, and it is
     * deliberately outside the page's own memory so relocating is just
     * changing this field. */
    uint8_t *memory;

    Offset<Row>  rows;
    Offset<Cell> cells;

    style::Set    styles;
    GraphemeAlloc grapheme_alloc;
    GraphemeMap   grapheme_map;
    StringAlloc   string_alloc;

    /* Cells that are part of a link, and the link data itself. A cell holds
       only a bit, so the map turns a linked cell's offset into an ID. */
    hyperlink::Map hyperlink_map;
    hyperlink::Set hyperlink_set;

    Capacity capacity;
    size_t   size;

    Page() : memory(nullptr), rows(), cells(), capacity(), size(0) {}

    /* Build a page in caller-provided memory, which must be at least
     * PageLayout::init(cap).total_size bytes and zero-filled.
     *
     * Only the row headers are written: every other member treats all-zero as
     * its empty state, so a fresh page leaves the rest of the pages untouched
     * until something actually uses them. */
    static Page init(uint8_t *buf, const Capacity &cap) {
        const PageLayout l = PageLayout::init(cap);
        OffsetBuf base = OffsetBuf::init(buf);

        Page p;
        p.memory = buf;
        p.capacity = cap;
        p.size = l.total_size;

        p.rows = base.member<Row>(l.rows_start);
        p.cells = base.member<Cell>(l.cells_start);

        p.styles = style::Set::init_assume_zeroed(
            base.add(l.styles_start), l.styles_layout, style::Context());
        p.grapheme_alloc = GraphemeAlloc::init_assume_zeroed(
            base.add(l.grapheme_alloc_start), l.grapheme_alloc_layout);
        p.grapheme_map = GraphemeMap::init_assume_zeroed(
            base.add(l.grapheme_map_start), l.grapheme_map_layout);
        p.string_alloc = StringAlloc::init_assume_zeroed(
            base.add(l.string_alloc_start), l.string_alloc_layout);

        p.hyperlink_map = hyperlink::Map::init_assume_zeroed(
            base.add(l.hyperlink_map_start), l.hyperlink_map_layout);
        p.hyperlink_set = hyperlink::Set::init_assume_zeroed(
            base.add(l.hyperlink_set_start), l.hyperlink_set_layout,
            hyperlink::Context());

        /* Point each row at its slice of the cell array. This is the one thing
         * a zeroed buffer cannot express, since row 0's cells are not at
         * offset 0. */
        Row *r = p.rows.ptr(buf);
        for (CellCountInt y = 0; y < cap.rows; y++) {
            Offset<Cell> o;
            o.offset = (OffsetInt)(l.cells_start +
                                   (size_t)y * cap.cols * sizeof(Cell));
            r[y].set_cells(o);
        }

        return p;
    }

    /* Move the page to a new base address.
     *
     * The caller copies `size` bytes; this just re-points the base. Nothing
     * else needs fixing up, which is the entire payoff of offset addressing. */
    void relocate(uint8_t *new_base) { memory = new_base; }

    /* ─── addressing ─────────────────────────────────────────────────────── */

    Row *get_row(CellCountInt y) {
        assert(y < capacity.rows);
        return &rows.ptr(memory)[y];
    }

    const Row *get_row(CellCountInt y) const {
        assert(y < capacity.rows);
        return &rows.ptr(memory)[y];
    }

    /* The cells of a row, `capacity.cols` of them. */
    Cell *get_cells(CellCountInt y) {
        return get_row(y)->cells().ptr(memory);
    }

    Cell *get_cell(CellCountInt x, CellCountInt y) {
        assert(x < capacity.cols);
        return &get_cells(y)[x];
    }

    /* ─── styles ─────────────────────────────────────────────────────────── */

    /* Assign a style to a cell, taking a reference to the new style and
     * releasing the old one.
     *
     * The new reference is taken before the old is released so that
     * re-applying the same style is a no-op rather than briefly dropping the
     * refcount to zero, which would make the style eligible for reaping
     * between the two calls.
     *
     * Returns false if the style set is full. */
    bool set_cell_style(CellCountInt x, CellCountInt y, const style::Style &s) {
        Cell *c = get_cell(x, y);
        const style::Id old_id = c->style_id();

        style::Id new_id = style::DEFAULT_ID;
        if (!s.is_default()) {
            if (styles.add(memory, s, &new_id) != AddResult::ok) return false;
        }

        if (old_id != style::DEFAULT_ID) styles.release(memory, old_id);

        c->set_style_id(new_id);

        if (new_id != style::DEFAULT_ID) {
            /* The row's styled flag is a false-positive-only hint: set once a
             * style is used, never cleared, because proving no cell is still
             * styled would cost more than the flag saves. */
            get_row(y)->set_styled(true);
        }

        return true;
    }

    /* The style of a cell, or the default when it has none. */
    style::Style get_cell_style(CellCountInt x, CellCountInt y) {
        const style::Id id = get_cell(x, y)->style_id();
        if (id == style::DEFAULT_ID) return style::Style();
        return *styles.get(memory, id);
    }

    /* Drop a cell's style reference and reset it to default. Used when
     * erasing, so that styles do not leak references. */
    void clear_cell_style(CellCountInt x, CellCountInt y) {
        Cell *c = get_cell(x, y);
        const style::Id id = c->style_id();
        if (id == style::DEFAULT_ID) return;
        styles.release(memory, id);
        c->set_style_id(style::DEFAULT_ID);
    }

    /* Erase a cell entirely, releasing anything it referenced. */
    void clear_cell(CellCountInt x, CellCountInt y) {
        clear_cell_style(x, y);
        *get_cell(x, y) = Cell();
    }

    /* Number of distinct styles currently interned. */
    size_t style_count() const { return styles.count(); }
};

/* ─── closing the hyperlink/page cycle ───────────────────────────────────── */

/* Declared in hyperlink.hpp against a forward-declared Page. */
inline void hyperlink::PageEntry::free(Page *page) const {
    if (!page) return;

    /* An explicit ID is its own allocation; an implicit one is just a
     * counter and owns nothing. */
    if (kind == IdKind::explicit_id && explicit_id.len > 0) {
        page->string_alloc.free(page->memory,
                                explicit_id.offset.ptr(page->memory),
                                explicit_id.len);
    }
    if (uri.len > 0) {
        page->string_alloc.free(page->memory, uri.offset.ptr(page->memory), uri.len);
    }
}

/* ─── page hyperlink operations ──────────────────────────────────────────── */

/* Hash for map keys that are cell offsets, used by both the grapheme map
 * and the hyperlink map.
 *
 * A cell offset is always a multiple of sizeof(Cell), so its low bits are
 * zero. The map takes its bucket index from the low bits of the hash, so
 * using the offset directly would leave most buckets unreachable and pile
 * every key into a fraction of the table. Mixing first spreads them. */
struct PageCellKeyHash {
    uint64_t operator()(OffsetInt off) const {
        uint64_t x = (uint64_t)off + 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }
};

inline uint64_t page_cell_key_hash(OffsetInt off) {
    return PageCellKeyHash()(off);
}

/* Copy bytes into the page's string storage, returning a slice.
 * Returns false if there is no room. */
inline bool page_alloc_string(Page *p, const char *s, size_t len,
                              Offset<uint8_t>::Slice *out) {
    out->len = 0;
    out->offset.offset = 0;
    if (len == 0) return true;

    uint8_t *dst = p->string_alloc.alloc<uint8_t>(p->memory, len);
    if (!dst) return false;

    memcpy(dst, s, len);
    out->offset = get_offset<uint8_t>(p->memory, dst);
    out->len = len;
    return true;
}

/* Attach a hyperlink to a cell.
 *
 * `id` may be null, in which case `implicit` distinguishes this link from
 * others — that is what keeps two separate OSC 8 runs to the same URI from
 * being treated as one link.
 *
 * Returns false and changes nothing if the strings or the map have no room.
 * The text still renders; only the link is dropped. */
inline bool page_set_cell_hyperlink(Page *p, CellCountInt x, CellCountInt y,
                                    const char *uri, size_t uri_len,
                                    const char *id, size_t id_len,
                                    OffsetInt implicit) {
    if (!p || !uri || uri_len == 0) return false;

    /* The set resolves offsets through the page, so it needs to know which
     * page before any lookup or insert. */
    p->hyperlink_set.context.base = p->memory;
    p->hyperlink_set.context.src_base = nullptr;
    p->hyperlink_set.context.page = p;

    hyperlink::PageEntry entry;
    if (!page_alloc_string(p, uri, uri_len, &entry.uri)) return false;

    if (id && id_len > 0) {
        entry.kind = hyperlink::PageEntry::IdKind::explicit_id;
        if (!page_alloc_string(p, id, id_len, &entry.explicit_id)) {
            /* Undo the URI, or it leaks for the page's lifetime. */
            p->string_alloc.free(p->memory, entry.uri.offset.ptr(p->memory),
                                 entry.uri.len);
            return false;
        }
    } else {
        entry.kind = hyperlink::PageEntry::IdKind::implicit_id;
        entry.implicit_id = implicit;
    }

    /* If an identical link is already interned, add() reports it through the
     * context's deleted hook, which releases the strings just allocated. */
    hyperlink::Id link_id = 0;
    if (p->hyperlink_set.add(p->memory, entry, &link_id) != AddResult::ok) {
        entry.free(p);
        return false;
    }

    Cell *c = p->get_cell(x, y);
    const OffsetInt cell_off = get_offset<Cell>(p->memory, c).offset;

    if (!p->hyperlink_map.put(p->memory, page_cell_key_hash(cell_off),
                              cell_off, link_id)) {
        p->hyperlink_set.release(p->memory, link_id);
        return false;
    }

    c->set_hyperlink(true);
    p->get_row(y)->set_hyperlink(true);
    return true;
}

/* The URI attached to a cell, or false if it has none.
 *
 * The returned pointer is into page memory and is invalidated by anything
 * that frees the link. */
inline bool page_get_cell_hyperlink(Page *p, CellCountInt x, CellCountInt y,
                                    const uint8_t **uri, size_t *uri_len) {
    if (!p) return false;

    Cell *c = p->get_cell(x, y);
    if (!c->hyperlink()) return false;

    const OffsetInt cell_off = get_offset<Cell>(p->memory, c).offset;
    hyperlink::Id *link_id = p->hyperlink_map.get(
        p->memory, page_cell_key_hash(cell_off), cell_off);
    if (!link_id) return false;

    p->hyperlink_set.context.base = p->memory;
    p->hyperlink_set.context.src_base = nullptr;
    p->hyperlink_set.context.page = p;

    hyperlink::PageEntry *e = p->hyperlink_set.get(p->memory, *link_id);
    if (!e) return false;

    *uri = e->uri.offset.ptr(p->memory);
    *uri_len = e->uri.len;
    return true;
}

/* Detach a cell's hyperlink, releasing the link if this was its last cell. */
inline void page_clear_cell_hyperlink(Page *p, CellCountInt x, CellCountInt y) {
    if (!p) return;

    Cell *c = p->get_cell(x, y);
    if (!c->hyperlink()) return;

    const OffsetInt cell_off = get_offset<Cell>(p->memory, c).offset;
    const uint64_t h = page_cell_key_hash(cell_off);

    hyperlink::Id *link_id = p->hyperlink_map.get(p->memory, h, cell_off);
    if (link_id) {
        p->hyperlink_set.context.base = p->memory;
        p->hyperlink_set.context.src_base = nullptr;
        p->hyperlink_set.context.page = p;

        p->hyperlink_set.release(p->memory, *link_id);
        p->hyperlink_map.remove(p->memory, h, cell_off, PageCellKeyHash());
    }

    /* The row's hyperlink flag is a false-positive-only hint like styled, so
     * it is deliberately not cleared here. */
    c->set_hyperlink(false);
}

/* ─── page grapheme operations ───────────────────────────────────────────── */

/* A cell holds one codepoint. Anything built from several — a base letter
 * plus combining marks, a flag, an emoji with a modifier — keeps its first
 * codepoint in the cell and the rest in the page's grapheme storage, found by
 * the cell's offset.
 *
 * Splitting it this way keeps the common case free: a cell of plain text
 * costs nothing extra, and only cells that actually need more pay for it. The
 * cell's tag says which case it is, and the row carries a flag so operations
 * that erase cells can skip grapheme cleanup entirely on rows that never had
 * any.
 *
 * Appending reallocates, which is quadratic in the number of codepoints on
 * one cell. Clusters are two or three codepoints in practice, so the simpler
 * code is worth more than the saved copies. */

/* The additional codepoints on a cell, beyond the one it holds directly.
 * Returns false if the cell has none. */
inline bool page_grapheme_codepoints(Page *p, CellCountInt x, CellCountInt y,
                                     const uint32_t **out, uint32_t *out_len) {
    if (!p) return false;

    Cell *c = p->get_cell(x, y);
    if (!c->has_grapheme()) return false;

    const OffsetInt cell_off = get_offset<Cell>(p->memory, c).offset;
    GraphemeSlice *slice = p->grapheme_map.get(
        p->memory, page_cell_key_hash(cell_off), cell_off);
    if (!slice || slice->len == 0) return false;

    Offset<uint32_t> o;
    o.offset = slice->offset;
    *out = o.ptr(p->memory);
    *out_len = slice->len;
    return true;
}

/* Append a codepoint to a cell's grapheme cluster.
 *
 * Returns false and changes nothing if the grapheme storage or map is full;
 * the cell keeps the codepoints it already had and still renders. */
inline bool page_append_grapheme(Page *p, CellCountInt x, CellCountInt y,
                                 uint32_t cp) {
    if (!p) return false;

    Cell *c = p->get_cell(x, y);
    const OffsetInt cell_off = get_offset<Cell>(p->memory, c).offset;
    const uint64_t h = page_cell_key_hash(cell_off);

    const uint32_t *old_cps = nullptr;
    uint32_t old_len = 0;
    GraphemeSlice *existing = p->grapheme_map.get(p->memory, h, cell_off);
    if (existing && existing->len > 0) {
        Offset<uint32_t> o;
        o.offset = existing->offset;
        old_cps = o.ptr(p->memory);
        old_len = existing->len;
    }

    uint32_t *dst = p->grapheme_alloc.alloc<uint32_t>(p->memory, old_len + 1);
    if (!dst) return false;

    for (uint32_t i = 0; i < old_len; i++) dst[i] = old_cps[i];
    dst[old_len] = cp;

    GraphemeSlice slice;
    slice.offset = get_offset<uint32_t>(p->memory, dst).offset;
    slice.len = old_len + 1;

    if (!p->grapheme_map.put(p->memory, h, cell_off, slice)) {
        p->grapheme_alloc.free(p->memory, dst, old_len + 1);
        return false;
    }

    /* The old run is only released once the new one is safely in the map, so
     * a failure above leaves the cell exactly as it was. */
    if (old_len > 0) {
        p->grapheme_alloc.free(p->memory, const_cast<uint32_t *>(old_cps), old_len);
    }

    c->set_content_tag(ContentTag::codepoint_grapheme);
    p->get_row(y)->set_grapheme(true);
    return true;
}

/* Drop a cell's extra codepoints, leaving the one in the cell itself. */
inline void page_clear_grapheme(Page *p, CellCountInt x, CellCountInt y) {
    if (!p) return;

    Cell *c = p->get_cell(x, y);
    if (!c->has_grapheme()) return;

    const OffsetInt cell_off = get_offset<Cell>(p->memory, c).offset;
    const uint64_t h = page_cell_key_hash(cell_off);

    GraphemeSlice *slice = p->grapheme_map.get(p->memory, h, cell_off);
    if (slice && slice->len > 0) {
        Offset<uint32_t> o;
        o.offset = slice->offset;
        p->grapheme_alloc.free(p->memory, o.ptr(p->memory), slice->len);
    }
    p->grapheme_map.remove(p->memory, h, cell_off, PageCellKeyHash());

    /* Back to an ordinary single-codepoint cell. The row's grapheme flag is a
     * false-positive-only hint, so it stays set. */
    c->set_content_tag(ContentTag::codepoint);
}

/* ─── erasing ────────────────────────────────────────────────────────────── */

/* Erase a cell, releasing everything it referenced.
 *
 * Page::clear_cell only knows about styles, because graphemes and hyperlinks
 * are reached through free functions declared after Page. This is the
 * complete version and is what erase paths should call — dropping a cell
 * without releasing its grapheme run or its link would strand both for the
 * page's lifetime. */
inline void page_erase_cell(Page *p, CellCountInt x, CellCountInt y) {
    if (!p) return;
    page_clear_grapheme(p, x, y);
    page_clear_cell_hyperlink(p, x, y);
    p->clear_cell_style(x, y);
    *p->get_cell(x, y) = Cell();
}

/* Erase a whole row. */
inline void page_erase_row(Page *p, CellCountInt y) {
    if (!p) return;

    /* The row flags are false-positive-only, so a row that never held a style,
     * grapheme or link can skip the per-cell release work entirely. This is
     * why those flags exist. */
    Row *row = p->get_row(y);
    const bool needs_release =
        row->styled() || row->grapheme() || row->hyperlink();

    if (needs_release) {
        for (CellCountInt x = 0; x < p->capacity.cols; x++) {
            page_erase_cell(p, x, y);
        }
    } else {
        Cell *cells = p->get_cells(y);
        for (CellCountInt x = 0; x < p->capacity.cols; x++) cells[x] = Cell();
    }

    row->set_dirty(true);
}

/* ─── closing the style/page cycle ───────────────────────────────────────── */

/* These were declared in style.hpp against a forward-declared Cell. Now that
 * Cell is complete they can be defined. */

inline bool style::Style::bg(const Cell *cell, const Palette *palette, RGB *out) const {
    /* A cell's own background wins over the style's, which is what makes
     * background-only cells able to skip the style map entirely. */
    if (cell) {
        switch (cell->content_tag()) {
            case ContentTag::bg_color_palette:
                if (!palette) return false;
                *out = (*palette)[cell->color_palette()];
                return true;
            case ContentTag::bg_color_rgb:
                *out = cell->color_rgb();
                return true;
            default:
                break;
        }
    }

    switch (bg_color.tag) {
        case StyleColor::Tag::none:
            return false;
        case StyleColor::Tag::palette:
            if (!palette) return false;
            *out = (*palette)[bg_color.palette];
            return true;
        case StyleColor::Tag::rgb:
            *out = bg_color.rgb;
            return true;
    }
    return false;
}

inline bool style::Style::bg_cell(Cell *out) const {
    switch (bg_color.tag) {
        case StyleColor::Tag::none:
            return false;
        case StyleColor::Tag::palette:
            out->set_content_tag(ContentTag::bg_color_palette);
            out->set_color_palette(bg_color.palette);
            return true;
        case StyleColor::Tag::rgb:
            out->set_content_tag(ContentTag::bg_color_rgb);
            out->set_color_rgb(bg_color.rgb);
            return true;
    }
    return false;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_PAGE_HPP */
