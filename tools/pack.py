"""Row packing for the JT-8000 panel.

Row height is the max of its members, so the only width worth spending is width
that removes a row from the tallest block. Two caps stop the greedy going daft:
a block is never widened past the width at which its widest group fits on one
line, and blocks with identical content always get identical widths.
"""
import json
B={b['name']:b for b in json.load(open('blocks.json'))}
PAD,TITLE,GROUP_H,GAP,GGAP=26,72,54,26,34

def cells(k):
    return {'knob':(int(160*k),int(194*k)),'fader':(int(69*k),int(318*k)),
            'combo':(int(450*k),int(45*k)),'toggle':(int(107*k),int(114*k))}

def item_size(it,k):
    """Cell size for one control. Combos carry their own width, sized to their
    longest option - a flat 300dp made a 3-character octave selector as wide as
    a 12-character sync division, and both far wider than the sync toggle."""
    C=cells(k)
    if it['k']=='stepgrid':
        return int(it['w']*k), int((it['h']+34)*k)
    cw,ch=C.get(it['k'],C['knob'])
    if it['k']=='combo' and 'w' in it:
        # +24dp on the right so two adjacent combos never butt together. The
        # LFO's Waveform and Sync were touching while Delay's Type and Sync had
        # a clear gap, only because Delay's cells happened to be wider than
        # their sprites.
        cw=int((it['w']+24)*k)
        ch=int(79*k)                     # 45dp control + 34dp legend
    # The legend can be wider than the control it names. lw is measured with
    # the real font at authoring size, so this is exact rather than a guess at
    # average character width - which is what kept letting OCTAVE spill onto
    # DETUNE and DRIVE onto BASS.
    lw=int(it.get('lw',0)*k)
    if 'sh' in it:
        sh=it['sh']
        lw=max(lw,int(sh.get('lw',0)*k))
        if sh['k']=='combo' and 'w' in sh:
            cw=max(cw,int((sh['w']+24)*k))
    return max(cw,lw),ch

def group_size(gr,inner,k):
    """Size of one group when flowed into `inner` width.

    A group flagged `col` stacks its controls vertically instead of flowing
    them, for cases where a column is the better shape.
    """
    if gr.get('col'):
        ws=[item_size(i,k) for i in gr['items']]
        return max(w for w,_ in ws), int(GROUP_H*k)+sum(h for _,h in ws)+GAP*(len(ws)-1)
    x=0; rowH=0; h=int(GROUP_H*k); wmax=0
    for it in gr['items']:
        cw,ch=item_size(it,k)
        if x and x+cw>inner: h+=rowH+GAP; x=0; rowH=0
        x+=cw+GAP; rowH=max(rowH,ch); wmax=max(wmax,x-GAP)
    return wmax,h+rowH

def reflow(b,w,k):
    """Groups flow in TWO dimensions inside a block.

    Stacking them in one column is what made Oscillator 1 over 1100dp tall for
    fourteen controls - five groups, five stacked rows. Flowing them side by
    side where they fit keeps the block closer to square and lets the whole
    panel sit at a larger cell size.
    """
    inner=w-PAD*2
    x=0; y=int(TITLE*k); rowH=0
    for gr in b['groups']:
        gw,gh=group_size(gr,inner,k)
        if x and x+gw>inner: y+=rowH+GGAP; x=0; rowH=0
        x+=gw+GGAP; rowH=max(rowH,gh)
    return y+rowH+PAD

def natural_max(n,k):
    """Width at which EVERY group fits on one line, side by side.

    Capping at the widest single group meant a block could never grow enough to
    put two groups next to each other, so Pitch Envelope stayed 618dp tall
    against the other envelopes' 340 - its depth knob was stranded on its own
    row. The useful ceiling is the sum, not the max.
    """
    # group_line_w, not a raw sum: a COLUMN group's useful width is its widest
    # item, not all of them added together. Summing let the Mixer's single
    # stack of five knobs claim 876dp and starve the Filter beside it.
    tot=0
    for gr in B[n]['groups']:
        tot+=group_line_w(gr,k)+GGAP
    return tot-GGAP+PAD*2

def group_line_w(gr,k):
    """Width this group needs to sit on one line - or one column, if flagged."""
    if gr.get('col'):
        return max(item_size(i,k)[0] for i in gr['items'])
    return (sum(item_size(i,k)[0] for i in gr['items'])
            + GAP*(len(gr['items'])-1))

NOWRAP_MAX = 5      # groups up to this many controls never wrap

def min_width(n,k):
    """Block width floor.

    Small groups are kept on one line - the LFO's Core group is Freq, Waveform
    and Sync, and breaking it put Sync into the Per-Destination legend below.
    Large groups are allowed to wrap: forcing Filter's seven-control Core onto
    one line needs 1220dp and costs the whole panel a cell size.
    """
    floors=[]
    for g in B[n]['groups']:
        if len(g['items'])<=NOWRAP_MAX:
            floors.append(group_line_w(g,k))
        else:
            floors.append(max(item_size(i,k)[0] for i in g['items']))
    return max(floors)+PAD*2

def members(entry):
    return list(entry) if isinstance(entry,tuple) else [entry]


def entry_w(entry,k,ws):
    return ws[entry]


def pack_row(names,avail,k):
    """Give width to whichever unit is tallest, until nothing more can be gained.

    Two rules that were wrong before:
      - the widen step was a whole knob cell, so a narrow block whose cap was
        less than one step away was never widened at all. Amp Envelope sat at
        282dp needing 344, and stayed two rows tall for four faders.
      - matched blocks were equalised AFTER the greedy had already pulled them
        apart, by which point the correction no longer fit. They are widened
        together as one unit instead.
    """
    C=cells(k); step=C['knob'][0]+GAP

    MATCH=[["Amp Envelope","Filter Envelope","Pitch Envelope"],
           ["Oscillator 1","Oscillator 2"],
           ["LFO 1","LFO 2"]]
    units,seen=[],set()
    for m in MATCH:
        grp=[n for n in m if n in names]
        if len(grp)>1:
            units.append(grp); seen.update(grp)
    for n in names:
        if n not in seen:
            units.append([n])

    cap={u_i:max(natural_max(m,k) for n in u for m in members(n))
         for u_i,u in enumerate(units)}
    w={u_i:max(min_width(m,k) for n in u for m in members(n))
       for u_i,u in enumerate(units)}
    for i in w:
        w[i]=min(w[i],cap[i])

    def cost(i):
        return len(units[i])
    spare=avail-sum(w[i]*cost(i) for i in w)

    def unit_h(i):
        # A stacked slot's height is the SUM of its members plus the gutter.
        out=0
        for n in units[i]:
            ms=members(n)
            h=sum(reflow(B[m],w[i],k) for m in ms)+G*(len(ms)-1)
            out=max(out,h)
        return out

    while spare>0:
        cands=[i for i in w if w[i]<cap[i] and (min(step,cap[i]-w[i])*cost(i))<=spare]
        if not cands: break
        t=max(cands,key=unit_h)
        inc=min(step,cap[t]-w[t])
        w[t]+=inc; spare-=inc*cost(t)

    def row_h():
        return max(unit_h(i) for i in w)

    for _ in range(60):
        h0 = row_h()
        tall = max(w, key=unit_h)
        if w[tall] >= cap[tall]:
            break
        donors = sorted([i for i in w if i != tall
                         and w[i] - step >= min(min_width(m, k)
                                        for n in units[i]
                                        for m in members(n))],
                        key=unit_h)
        moved = False
        for src in donors:
            take = step * cost(src)
            give = step * cost(tall)
            if take < give:
                continue
            w[src] -= step
            w[tall] += min(step, cap[tall] - w[tall])
            if row_h() < h0 and max(unit_h(i) for i in w) <= h0:
                moved = True
                break
            w[src] += step
            w[tall] -= min(step, cap[tall] - w[tall])
        if not moved:
            break

    return [w[next(i for i,u in enumerate(units) if n in u)] for n in names]

# A row entry may be a tuple, meaning those blocks share one column slot and
# stack vertically. Mixer alone as a column was 1100dp tall and set row 1's
# height on its own; paired with Filter the slot is used properly and both
# blocks keep their controls in rows.
STACK=("Mixer","Filter")

ROWS=[["Oscillator 1","Oscillator 2",STACK],
      ["Amp Envelope","Filter Envelope","Pitch Envelope","LFO 1","LFO 2"],
      ["EQ & Drive","Mod FX","Delay","Output Mix"]]
CW,CH,M,G=3840,2160,40,24   # margin and gutter
HEADER=260                  # top strip: brand, LCD, preset, layers, shift

LAYER_BLOCKS=["Reverb","Step Sequencer","Arpeggiator","Setup"]


def build(k):
    """Pack the panel.

    Rows 1 and 2 pack normally. The third row is the LAYER AREA: row 3's blocks
    are the Main layer's content, and Reverb, Step Sequencer and Arpeggiator are
    alternatives occupying the same footprint. Its height is the tallest of all
    of them.

    Layers used to be positioned by hand after the rows were packed, which made
    them the one thing on the panel outside the containment rules - and the
    sequencer, being twice row 3's height, was drawn straight through row 2.
    """
    place={}; y=M+HEADER+G
    for row in ROWS[:-1]:
        avail=CW-2*M-G*(len(row)-1)
        ws=pack_row(row,avail,k)
        x=M+max(0,(avail-sum(ws))//2); rh=0
        for n,w in zip(row,ws):
            cy=y
            for m in members(n):
                h=reflow(B[m],w,k)
                place[m]=(x,cy,w,h)
                cy+=h+G
            x+=w+G
            rh=max(rh,cy-G-y)
        y+=rh+G
    # --- layer area ---------------------------------------------------------
    main_row = ROWS[-1]
    avail = CW - 2 * M - G * (len(main_row) - 1)
    ws = pack_row(main_row, avail, k)
    main_h = max(reflow(B[n], w, k) for n, w in zip(main_row, ws))

    w_full = CW - 2 * M
    lh = {n: reflow(B[n], w_full, k) for n in LAYER_BLOCKS}
    area_h = max(main_h, max(lh.values()))

    x = M + max(0, (avail - sum(ws)) // 2)
    for n, w in zip(main_row, ws):
        place[n] = (x, y, w, reflow(B[n], w, k))
        x += w + G
    for n in LAYER_BLOCKS:
        place[n] = (M, y, w_full, lh[n])

    return place, y + area_h + M
