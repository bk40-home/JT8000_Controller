#!/usr/bin/env python3
"""Rebuild blocks.json.

Two things a combo needs that it did not have:
  w  its own width, sized to its longest option. Every combo was a fixed 300dp
     bar, so a 3-character octave selector was as wide as a 12-character sync
     division and far wider than the 107dp sync toggle beside it.
  v  the currently selected value, to be drawn INSIDE the control. A combo that
     shows no value is just a bar; the user cannot read the patch from it.
"""
import yaml, json, collections
from PIL import ImageFont

# --- text measurement -------------------------------------------------------
# Every collision since the layout rewrite traced back to guessing text width
# from character count. Legends are measured ONCE here with the real font, at
# authoring size, and the packer multiplies by the cell scale. No estimates.
FONT_DIR='/usr/share/fonts/truetype/crosextra/'
F_LAB=ImageFont.truetype(FONT_DIR+'Carlito-Regular.ttf', 28)   # legend, 28dp
F_VAL=ImageFont.truetype(FONT_DIR+'Carlito-Bold.ttf', 27)      # value, 27dp
def text_w(font, t):
    return int(font.getlength(t)) + 1

d=yaml.safe_load(open('fw/Jteensy8000-master/jt8000-params/params.yaml'))
OPT=d['option_sets']
secs={s['index']:s for s in d['sections']}
groups=collections.defaultdict(lambda: collections.defaultdict(list))
for p in d['params']:
    s=secs[p['ui']['section']]
    g=(s.get('groups') or ['Main'])[p['ui'].get('group',0)]
    groups[s['name']][g].append(p)

def kind(p):
    if p['type']=='select': return 'combo'
    if p['type']=='toggle': return 'toggle'
    if p.get('widget')=='envelope': return 'fader'
    return 'knob'

# Combo width: the widest option MEASURED, plus the text inset and the arrow
# well the sprite reserves on its right (60dp, matching gen_combo's cap).
INSET, ARROW, MINW = 30, 60, 120
def combo_w(p):
    opts=OPT.get(p.get('options')) or []
    widest=max((text_w(F_VAL,o) for o in opts), default=60)
    return max(MINW, widest + INSET + ARROW)
def combo_v(p):
    opts=OPT.get(p.get('options')) or []
    return opts[min(p.get('default',0), len(opts)-1)] if opts else ''

# Shift membership is computed per section, not by leaf name: 'depth' and
# 'destination' are LFO trims, but the step sequencer has its own depth and
# destination which must stay on the panel. A parameter is hidden only if some
# control IN THE SAME SECTION pairs to it.
SHIFT=set()

# Which control each shift trim borrows. A trim sits on the control it trims -
# a curve on its own envelope stage - so shift never moves anything, it only
# relabels in place.
SHIFT_PAIRS={'attack':'attack_curve','decay':'decay_curve',
             'release':'release_curve','freq':'depth','waveform':'destination'}
DROP=set()
AB={'ATTACK':'ATT','DECAY':'DEC','SUSTAIN':'SUS','RELEASE':'REL',
 'SUPERSAW DETUNE':'SS DETUNE','SUPERSAW MIX':'SS MIX',
 'FEEDBACK AMOUNT':'FB AMOUNT','FEEDBACK MIX':'FB MIX',
 'PWM LFO1 DEPTH':'PWM DEPTH','CROSS MOD':'X-MOD','RESONANCE':'RESO',
 'PITCH OFFSET':'OCTAVE','FINE TUNE':'FINE','KEY TRACK':'KEY TRK',
 'ENV AMOUNT':'ENV AMT','OCTAVE CONTROL':'OCT CTRL',
 'OBXA XPANDER MODE':'XPANDER','PITCH MOD DEST':'MOD DEST',
 'DELAY FEEDBACK':'FEEDBACK','DELAY EFFECT':'TYPE','DELAY TIME':'TIME',
 'DELAY MIX':'MIX','DELAY SYNC':'SYNC','MOD EFFECT':'TYPE',
 'MOD FEEDBACK':'FEEDBACK','MOD RATE':'RATE','MOD MIX':'MIX',
 'BASS GAIN':'BASS','TREBLE GAIN':'TREBLE','UNISON DETUNE':'UNI DET',
 'PITCH DEPTH':'PITCH','FILTER DEPTH':'FILTER','PWM DEPTH':'PWM',
 'AMP DEPTH':'AMP','MASTER VOLUME':'VOLUME',
 'OBXA MULTIMODE':'MULTI','OBXA RES MOD DEPTH':'RES MOD',
 'ATTACK CURVE':'A CRV','DECAY CURVE':'D CRV',
 'RELEASE CURVE':'R CRV'}
def legend(key):
    t=key.split('.')[-1].replace('_',' ').upper(); return AB.get(t,t)

BYKEY={}

def _cell(p):
    k=kind(p); t=legend(p['key'])
    e=dict(k=k,t=t,key=p['key'],lw=text_w(F_LAB,t))
    if k=='combo':
        e['w']=combo_w(p); e['v']=combo_v(p)
        e['opts']=OPT.get(p.get('options')) or []
    return e

# Step editing is already select-a-step-then-edit in the parameter model, so
# these collapse into one <stepgrid> element: it owns step selection and the
# per-step value, and shows the pattern's shape, which a pair of knobs cannot.
STEPGRID={
 'Step Sequencer':dict(label='Steps',lanes=2,w=1600,h=190,
   keys={'step_select','step_value','aux_step_select','aux_step_value'}),
 'Arpeggiator':dict(label='Steps',lanes=3,w=2200,h=200,
   keys={'step_select','step_onoff','step_accent','step_ratchet'}),
}


def item(p):
    e=_cell(p)
    leaf=p['key'].split('.')[-1]
    alt=SHIFT_PAIRS.get(leaf)
    if alt:
        sec=p['key'].rsplit('.',1)[0]
        q=BYKEY.get(sec+'.'+alt)
        if q is not None:
            # The shift cell must fit the same footprint, so its measured width
            # is folded into the host's cell.
            e['sh']=_cell(q)
    return e

# Filter: the continuous controls you reach for constantly come first as one
# group, then the selectors that set the filter's character. Previously Core
# held four knobs while Oct Ctrl was stranded beside a column of three combos
# and Xpander sat marooned below them.
FILTER_GROUPS=[("Core",["cutoff","resonance","env_amount","key_track",
                        "octave_control","obxa_multimode",
                        "obxa_res_mod_depth"]),
               ("Engine",["engine","mode","va_type","obxa_xpander_mode"])]

# Mixer: five level knobs together, then the cross-modulation controls. The
# Levels group was wrapping after four because the block was too narrow.
# Mixer keeps only its levels. Cross-mod belongs with Oscillator 1 (it is
# oscillator 2 modulating oscillator 1), sync belongs with Oscillator 2 (it is
# oscillator 2 that gets synced), and the pitch-mod destination sits with
# Oscillator 1's arbitrary controls. That leaves the mixer as one clean column.
MIXER_GROUPS=[("Levels",["balance","osc1","osc2","sub","noise"])]

# Wave and Octave stack instead of sitting side by side: two combos in a
# column are about as tall as the knob row beside them, and stacking frees the
# width that was pushing OCTAVE's legend onto DETUNE.
# Groups laid out as a column rather than a row. Row 1's height is set by the
# oscillators, so Mixer's five level knobs and Filter's four engine selectors
# can run vertically into height that was standing empty anyway - and both read
# better stacked: a mixer is a set of levels, and the engine selectors are a
# chain of choices.
# Only the oscillator Wave/Octave pair stacks now. Mixer's levels and Filter's
# engine selectors read better as rows, and the two blocks share one column
# slot stacked vertically instead - see STACKS in pack.py.
COLUMN_GROUPS={"Wave"}

# Borrowed keys are written in full (section.key); bare names come from the
# oscillator's own section.
OSC1_GROUPS=[("Wave",["wave","pitch_offset"]),
             ("Tuning & PWM",["fine_tune","detune","pwm_lfo1_depth"]),
             ("Supersaw",["supersaw_detune","supersaw_mix"]),
             ("Feedback",["feedback_amount","feedback_mix"]),
             ("DC & Ring",["freq_dc","shape_dc","ring_mix","mix.cross_mod"]),
             ("Arbitrary",["arb_bank","arb_index","mix.pitch_mod_dest"])]
OSC2_GROUPS=[("Wave",["wave","pitch_offset"]),
             ("Tuning & PWM",["fine_tune","detune","pwm_lfo1_depth"]),
             ("Supersaw",["supersaw_detune","supersaw_mix"]),
             ("Feedback",["feedback_amount","feedback_mix"]),
             ("DC & Ring",["freq_dc","shape_dc","ring_mix","mix.osc_sync"]),
             ("Arbitrary",["arb_bank","arb_index"])]

ORDER={'knob':0,'fader':0,'toggle':1,'combo':2}
def order_items(items):
    """Knobs first, then toggles, then selectors.

    Within a group the continuous controls are what you sweep while listening;
    the selectors are set once. Putting them in that order also stops a tall
    knob row sitting under a short combo row and wasting the difference.
    """
    return sorted(items, key=lambda i: ORDER.get(i['k'],0))

def merge_singletons(gs):
    out=[]
    for g in gs:
        if (len(g['items'])==1 and out
                and g['items'][0]['k']!='stepgrid'
                and out[-1]['items'][-1]['k']!='stepgrid'):
            out[-1]['items'].extend(g['items'])
        else: out.append(g)
    if len(out)>1 and len(out[0]['items'])==1:
        out[1]['items']=out[0]['items']+out[1]['items']; out.pop(0)
    return out

BLOCKS=[("Oscillator 1",[("Oscillator 1",None)]),("Oscillator 2",[("Oscillator 2",None)]),
 ("Mixer",[("Mixer",None)]),("Filter",[("Filter",None)]),
 ("Amp Envelope",[("Amp Envelope",None)]),("Filter Envelope",[("Filter Envelope",None)]),
 ("Pitch Envelope",[("Pitch Envelope",None)]),("LFO 1",[("LFO 1",None)]),("LFO 2",[("LFO 2",None)]),
 ("EQ & Drive",[("Effects","EQ & Drive")]),("Mod FX",[("Effects","Modulation")]),
 ("Delay",[("Effects","Delay")]),("Reverb",[("Global Reverb",None)]),
 ("Output Mix",[("Effects","Output"),("Master",None)]),
 # Set-and-leave controls become a fifth layer rather than taking permanent
 # panel area; four separate blocks would each be too small to justify one.
 ("Setup",[("Voice Mode",None),("Performance",None),
           ("BPM Clock",None),("Velocity",None)]),
 ("Step Sequencer",[("Step Sequencer",None)]),
 ("Arpeggiator",[("Arpeggiator",None)])]

BYKEY.update({p['key']:p for p in d['params']})

# Legends of the parameters the stepgrid absorbs, so they are not also drawn as
# individual controls.
SG_LEGENDS={n:{legend('x.'+k) for k in v['keys']} for n,v in STEPGRID.items()}

for _p in d['params']:
    _sec, _leaf = _p['key'].rsplit('.', 1)
    for _host, _alt in SHIFT_PAIRS.items():
        if _leaf == _alt and (_sec + '.' + _host) in BYKEY:
            SHIFT.add(_p['key'])
            break

out=[]
for name,srcs in BLOCKS:
    gs=[]
    if name.startswith("Oscillator"):
        pre='osc1.' if name.endswith('1') else 'osc2.'
        spec=OSC1_GROUPS if name.endswith('1') else OSC2_GROUPS
        byk={p['key'].split('.')[-1]:p for g in groups[name].values() for p in g}
        allk={p['key']:p for p in d['params']}
        for lbl,keys in spec:
            items=[]
            for kk in keys:
                p=allk.get(kk) if '.' in kk else byk.get(kk)
                if p is not None and p['key'] not in DROP: items.append(p)
            if items: gs.append(dict(label=lbl, items=[item(p) for p in items]))
    elif name in ("Filter","Mixer"):
        spec = FILTER_GROUPS if name=="Filter" else MIXER_GROUPS
        byk={p['key'].split('.')[-1]:p for g in groups[name].values() for p in g}
        for lbl,keys in spec:
            items=[byk[k] for k in keys if k in byk]
            if items: gs.append(dict(label=lbl, items=[item(p) for p in items]))
    else:
        for sec,grp in srcs:
            for g,items in groups[sec].items():
                if grp is not None and g!=grp: continue
                items=[p for p in items if p['key'] not in DROP]
                prim=[p for p in items if p['key'] not in SHIFT] or items
                if prim: gs.append(dict(label=g, items=[item(p) for p in prim]))
    for g in gs:
        g['items']=order_items(g['items'])
        if g['label'] in COLUMN_GROUPS: g['col']=1
    sg=STEPGRID.get(name)
    if sg:
        out_gs=[]
        for g in gs:
            keep=[i for i in g['items'] if i['t'] not in SG_LEGENDS[name]]
            if keep: out_gs.append(dict(g, items=keep))
        out_gs.append(dict(label=sg['label'],
                           items=[dict(k='stepgrid', t='PATTERN',
                                       key=name.lower().replace(' ','_')+'.pattern',
                                       lw=text_w(F_LAB,'PATTERN'),
                                       w=sg['w'], h=sg['h'],
                                       lanes=sg['lanes'])]))
        gs=out_gs
    gs=merge_singletons(gs)
    out.append(dict(name=name,groups=gs,n=sum(len(g['items']) for g in gs)))
shown={i['t'] for b in out for g in b['groups'] for i in g['items']}
shown |= {i['sh']['t'] for b in out for g in b['groups']
          for i in g['items'] if 'sh' in i}
for n,v in STEPGRID.items():
    shown |= SG_LEGENDS[n]
missing=[]
for p in d['params']:
    leaf=p['key'].split('.')[-1]
    if p['key'] in DROP or p['key'] in SHIFT:
        continue
    if legend(p['key']) not in shown:
        missing.append(p['key'])
if missing:
    raise SystemExit("ABORT: %d parameters neither shown, shifted nor dropped:\n  %s"
                     % (len(missing), "\n  ".join(missing)))
print("gate: every parameter accounted for")

json.dump(out,open('blocks.json','w'),separators=(',',':'))
ws=[i['w'] for b in out for g in b['groups'] for i in g['items'] if i['k']=='combo']
lws=[i['lw'] for b in out for g in b['groups'] for i in g['items']]
print('controls',sum(b['n'] for b in out),'combos',len(ws),
      'combo widths %d..%d  legend widths %d..%d (measured)'
      %(min(ws),max(ws),min(lws),max(lws)))
