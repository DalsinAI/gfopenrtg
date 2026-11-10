#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Checks mesa/patches/0014 (nir: compact algebraic tables) against Mesa's own
generator: nir_opt_algebraic.c made with the patch must drive the search
automaton exactly as the one made without it.

  tests/nir_tables_check.py [WORK]

WORK (default /tmp) gets two copies of the pinned Mesa (mesa/UPSTREAM.json's
tarball, which mesa/build.sh has fetched into its work directory, or give
MESA_TARBALL): one with the patches before 0014, one with all of them. Each
generates nir_opt_algebraic.c (needs a python3 with mako, PYTHON). Then, for
every pass and every opcode, it reads the tables back out of both C files
and checks:
  - the same filter (state -> filtered state) for every state;
  - the same number of filtered states;
  - the same next state for every combination of the sources' filtered
    states, looked up as nir_search.c looks it up (dense, or a row table);
  - for every state, the same transforms in the same order;
  - everything else in the file (the search values, the conditions and the
    pass functions) the same text.
It prints a line a pass and "nir tables: same" at the end, or fails.
"""
import itertools, json, os, re, subprocess, sys, tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
GL = os.path.dirname(HERE)
WORK = sys.argv[1] if len(sys.argv) > 1 else "/tmp"
PY = os.environ.get("PYTHON", sys.executable)


def tree(name, patches):
    up = json.load(open(os.path.join(GL, "mesa", "UPSTREAM.json")))
    tb = os.environ.get("MESA_TARBALL") or os.path.join(GL, "build", up["file"])
    d = os.path.join(WORK, name)
    src = os.path.join(d, "mesa-" + up["version"])
    if not os.path.isdir(src):
        os.makedirs(d, exist_ok=True)
        with tarfile.open(tb) as t:
            t.extractall(d, members=[m for m in t.getmembers() if "/src/compiler/" in m.name], filter="data")
        for p in patches:
            # only the NIR part of each patch matters here; the others may not apply to a part tree
            subprocess.run(["patch", "-s", "-p1", "-d", src, "-i", p, "-f", "-r", "-"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    nir = os.path.join(src, "src", "compiler", "nir")
    out = os.path.join(d, "nir_opt_algebraic.c")
    if not os.path.exists(out):
        subprocess.check_call([PY, "-B", os.path.join(nir, "nir_opt_algebraic.py"), "--out", out], cwd=nir)
    return open(out).read()


def nums(s):
    return [int(x) for x in re.findall(r"-?\d+", s)]


def passes(c):
    """pass name -> (ops, transforms, offsets, rest of the text)"""
    res = {}
    arrays = {m.group(2): nums(m.group(3)) for m in re.finditer(
        r"static const (uint8_t|uint16_t) (\w+)\[\d*\] = \{([^}]*)\};", c)}
    for m in re.finditer(r"static const struct per_op_table (\w+)_pass_op_table\[nir_num_search_ops\] = \{\n(.*?)\n\};\n", c, re.S):
        name, body = m.group(1), m.group(2)
        ops = {}
        nstates = len(nums(re.search(r"static const uint16_t %s_transform_offsets\[\] = \{(.*?)\};" % name, c, re.S).group(1)))
        for e in re.finditer(r"\[(nir_\w+)\] = \{(.*?)\n   \},", body, re.S):
            op, b = e.group(1), e.group(2)
            f = re.search(r"\.filter = (NULL|\w+|\(const uint16_t \[\]\) \{([^}]*)\})", b)
            filt = None
            if f.group(1) != "NULL":
                filt = nums(f.group(2)) if f.group(2) is not None else arrays[f.group(1)]
            pg = re.search(r"\.filter_pages = (\w+)", b)
            if pg:
                # paged (nir_search.h, nir_search_filter): a block number for each PAGE states
                pages, P = arrays[pg.group(1)], 32
                filt = [pages[filt[s // P] * P + s % P] for s in range(nstates)]
            n = int(re.search(r"\.num_filtered_states = (\d+)", b).group(1))
            t = re.search(r"\.table = (\w+|\(const uint16_t \[\]\) \{([^}]*)\})", b)
            table = nums(t.group(2)) if t.group(2) is not None else arrays[t.group(1)]
            rt = re.search(r"\.row_table = (\d)", b)
            ops[op] = (filt, n, table, int(rt.group(1)) if rt else 0)
        tr = re.search(r"static const struct transform %s_transforms\[\] = \{(.*?)\n\};" % name, c, re.S).group(1)
        xf = []
        for x in re.findall(r"\{ ([^}]*) \}", tr):
            xf.append(None if "~0" in x or "UINT16_MAX" in x else tuple(nums(x)))
        offs = nums(re.search(r"static const uint16_t %s_transform_offsets\[\] = \{(.*?)\};" % name, c, re.S).group(1))
        res[name] = (ops, xf, offs)
    # everything but the transforms, tables and offsets
    rest = re.sub(r"static const struct transform \w+_transforms\[\] = .*?(?=static const nir_algebraic_table)", "", c, flags=re.S)
    return res, rest


def lookup(table, n, k, row_table, fs):
    index, rows = 0, 1
    head = k - 1 if row_table else k
    for i, f in enumerate(fs):
        if i == head:
            index = rows + table[index] * n + f
        else:
            index = index * n + f
            rows *= n
    return table[index]


def xlist(xf, o):
    out = []
    while xf[o] is not None:
        out.append(xf[o]); o += 1
    return out


def main():
    pdir = os.path.join(GL, "mesa", "patches")
    ps = sorted(os.path.join(pdir, p) for p in os.listdir(pdir) if p.endswith(".diff"))
    before = [p for p in ps if os.path.basename(p) < "0014"]
    old, old_rest = passes(tree("nirtab-before-0014", before))
    new, new_rest = passes(tree("nirtab-with-0014", ps))
    assert sorted(old) == sorted(new), (sorted(old), sorted(new))
    assert old_rest == new_rest, "the rest of nir_opt_algebraic.c differs"
    for name in old:
        o_ops, o_xf, o_offs = old[name]
        n_ops, n_xf, n_offs = new[name]
        assert sorted(o_ops) == sorted(n_ops), name
        lookups = 0
        for op in o_ops:
            ofilt, on, otab, _ = o_ops[op]
            nfilt, nn, ntab, nrt = n_ops[op]
            assert on == nn, (name, op)
            assert ofilt == nfilt, (name, op, "filter")
            # the number of sources: the dense table has n^k entries (n = 1: one entry, k from the op)
            k = next(k for k in range(1, 5) if on ** k == len(otab)) if on > 1 else 1
            for fs in itertools.product(range(on), repeat=k):
                assert lookup(otab, on, k, 0, fs) == lookup(ntab, nn, k, nrt, fs), (name, op, fs)
                lookups += 1
        assert len(o_offs) == len(n_offs), name
        for s in range(len(o_offs)):
            assert xlist(o_xf, o_offs[s]) == xlist(n_xf, n_offs[s]), (name, "state", s)
        print(f"{name}: {len(o_ops)} opcodes, {lookups} transitions, {len(o_offs)} states: same")
    print("nir tables: same")


main()
