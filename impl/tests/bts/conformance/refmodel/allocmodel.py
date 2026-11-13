"""Reference model of the allocator interface (spec 05 §1) and the two allocators of §3.

Addresses are offsets from the start of the allocator's region; the region start is aligned to
REGION_ALIGN. Every successful allocate is non-null, aligned, and disjoint from live blocks
(FC-02); the arena's offsets are exact because §3 fixes its algorithm; the pool's choice of
free block is Free Choice, so its expectation is the set of blocks that may be returned.
"""

REGION_ALIGN = 16
ERRORS = ("out_of_memory", "unsupported_alignment", "size_overflow")


def pow2(a):
    return a > 0 and a & (a - 1) == 0


def round_up(x, a):
    return (x + a - 1) // a * a


class Arena:
    """05 §3: bump cursor; deallocate of the last block moves the cursor back; reset empties it"""

    def __init__(self, capacity, max_align=REGION_ALIGN):
        self.cap, self.max_align = capacity, max_align
        self.cursor, self.stack = 0, []           # stack of (start, end_before) to undo the last block

    def allocate(self, size, align):
        if not pow2(align) or align > self.max_align:
            return ("err", "unsupported_alignment")
        start = round_up(self.cursor, align)
        if size == 0 and start <= self.cap:
            return ("ok", start)                  # a zero-size block takes no space and is not a "last block"
        if start + size > self.cap:
            return ("err", "out_of_memory")
        self.stack.append((start, self.cursor))
        self.cursor = start + size
        return ("ok", start)

    def deallocate(self, p, size, align):
        if size == 0:
            return ("done", None)
        if self.stack and self.stack[-1][0] == p and p + size == self.cursor:
            self.cursor = self.stack.pop()[1]
        return ("done", None)

    def reset(self):
        self.cursor, self.stack = 0, []
        return ("done", None)


class Pool:
    """05 §3: k blocks of one size; allocate takes any free block (Free Choice), deallocate returns it"""

    def __init__(self, block, count):
        self.block, self.count, self.live = block, count, 0

    def allocate(self, size, align):
        if not pow2(align) or align > REGION_ALIGN:
            return ("err", "unsupported_alignment")
        if size > self.block or self.live == self.count:
            return ("err", "out_of_memory")
        self.live += 1
        return ("ok", "free_block")

    def deallocate(self, p, size, align):
        self.live -= 1
        return ("done", None)


def expect(steps, alloc):
    """fill in `expect` for each step by running the model; blocks are named by `as`"""
    blocks, out = {}, []
    for st in steps:
        st = dict(st)
        op = st["op"]
        if op == "allocate":
            r = alloc.allocate(st["size"], st["align"])
            st["expect"] = {r[0]: str(r[1]) if isinstance(r[1], int) else r[1]}
            if r[0] == "ok" and "as" in st:
                blocks[st["as"]] = (r[1], st["size"])
        elif op == "deallocate":
            if st["block"] in blocks:             # a block whose allocate failed is not live
                alloc.deallocate(blocks.pop(st["block"])[0], st["size"], st["align"])
        elif op == "reallocate":                  # failure must leave the block as it was
            p, old = blocks[st["block"]]
            if st["new"] > alloc.cap:
                st["expect"] = {"err": "out_of_memory"}
            else:
                raise ValueError("only failing reallocate is modelled")
        elif op == "reset":
            if any(size for _, size in blocks.values()):
                raise ValueError("05 §3: reset needs every block from the arena to be returned")
            alloc.reset()
            blocks.clear()
        elif op not in ("fill", "check_contents"):
            raise ValueError(op)
        out.append(st)
    return out


def checked_size(count, elem, usize_bits):
    """05 §1: count * elem computed with chk arithmetic; a size above the largest idiff (04 §3, gate 13)
    is size_overflow before any call"""
    n = count * elem
    return ("err", "size_overflow") if n >= 1 << (usize_bits - 1) else ("ok", n)
