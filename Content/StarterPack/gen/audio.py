"""Sound effects and music, synthesised with ``spk.synth`` (no recordings, nothing copied), plus the audio INI files.

Layout follows the engine's audio path building (AudioEventRTS::generateFilenamePrefix):
    sound effects  Data\\Audio\\Sounds\\<name>.wav     (events list the names without extension in ``Sounds =``)
    music          Data\\Audio\\Tracks\\<Filename>      (MusicTrack ``Filename =``)
Short effects are 16 bit PCM; music is IMA ADPCM to keep the download small.

The INI files that name these events are produced here too, from the same tables, so a name can never be
spelled differently in the sound table and in the event definition.
"""

import math

from spk import synth as S
from spk.synth import RATE
from spk.util import Rng
from spk.wavfile import write_wav, write_ima_adpcm


# ------------------------------------------------------------------------------------------------- effects

def _blip(freq, ms, shape="sine", freq_end=None, tau=0.03, gain=0.8):
    n = ms / 1000.0
    return S.fade_edges(S.scale(S.exp_decay(S.osc(freq, n, shape, freq_end=freq_end), tau), gain), 2)


def gui_click():
    return S.mix(_blip(1200, 45, "square", tau=0.012, gain=0.35), _blip(2400, 30, "sine", tau=0.008, gain=0.3))


def gui_click_disabled():
    return S.concat(_blip(180, 70, "saw", tau=0.05, gain=0.5), S.silence(0.03), _blip(150, 90, "saw", tau=0.06, gain=0.5))


def gui_command_click():
    return S.concat(_blip(660, 40, "tri", tau=0.03), _blip(990, 70, "tri", tau=0.045))


def gui_combo_click():
    return _blip(740, 35, "sine", tau=0.02)


def gui_message():
    return S.concat(_blip(880, 90, "sine", tau=0.07), _blip(1320, 160, "sine", tau=0.1))


def gui_blip():
    return _blip(1000, 60, "tri", freq_end=1400, tau=0.05)


def gui_type():
    return _blip(1800, 18, "square", tau=0.006, gain=0.25)


def money():
    return S.concat(_blip(1568, 60, "sine", tau=0.05), _blip(2093, 220, "sine", tau=0.12))


def no_can_do():
    return S.concat(_blip(220, 110, "square", tau=0.09, gain=0.4), S.silence(0.04), _blip(165, 160, "square", tau=0.1, gain=0.4))


def radar_ping():
    return S.concat(_blip(1320, 90, "sine", tau=0.08), S.silence(0.05), _blip(1320, 90, "sine", tau=0.08),
                    S.silence(0.05), _blip(990, 200, "sine", tau=0.14))


def rifle_fire():
    crack = S.exp_decay(S.highpass(S.noise(0.18, 3), 1200), 0.02)
    body = S.exp_decay(S.lowpass(S.noise(0.2, 4), 1800), 0.04)
    thump = S.exp_decay(S.osc(180, 0.12, "sine", freq_end=70), 0.03)
    return S.fade_edges(S.normalize(S.mix(crack, body, thump, gains=[0.7, 0.5, 0.8]), 0.8), 1)


def rocket_fire():
    n = S.noise(0.9, 5)
    sweep = []
    # a whoosh: noise through a low pass whose cut off rises then falls
    y = 0.0
    for i, s in enumerate(n):
        t = i / len(n)
        cutoff = 500 + 2600 * math.sin(math.pi * min(1.0, t * 1.2)) ** 1.5
        a = 1.0 - math.exp(-2 * math.pi * cutoff / RATE)
        y += a * (s - y)
        sweep.append(y)
    env = S.envelope(sweep, attack=0.04, release=0.5)
    thump = S.exp_decay(S.osc(120, 0.25, "sine", freq_end=45), 0.07)
    return S.fade_edges(S.normalize(S.mix(env, thump, gains=[0.9, 0.9]), 0.8), 2)


def cannon_fire():
    boom = S.exp_decay(S.osc(95, 0.7, "sine", freq_end=32), 0.14)
    rumble = S.exp_decay(S.lowpass(S.noise(0.7, 6), 600), 0.16)
    crack = S.exp_decay(S.highpass(S.noise(0.1, 7), 900), 0.015)
    return S.fade_edges(S.normalize(S.mix(boom, rumble, crack, gains=[1.0, 0.8, 0.5]), 0.9), 1)


def explosion(seconds, depth):
    n = S.noise(seconds, 8 + int(seconds * 10))
    rumble = S.exp_decay(S.lowpass(n, 700 / depth + 200), seconds / 3.5)
    boom = S.exp_decay(S.osc(70 / depth, seconds, "sine", freq_end=24), seconds / 5)
    crack = S.exp_decay(S.highpass(S.noise(0.15, 9), 700), 0.03)
    return S.fade_edges(S.normalize(S.mix(rumble, boom, crack, gains=[1.0, 1.0, 0.45]), 0.92), 1)


def explosion_small():
    return explosion(0.8, 1.0)


def explosion_big():
    return explosion(1.5, 1.6)


def building_collapse():
    rumble = S.envelope(S.lowpass(S.noise(2.4, 21), 420), attack=0.05, release=1.2, curve=1.5)
    crunch = []
    rng = Rng(77)
    track = [0.0] * int(2.4 * RATE)
    for k in range(18):
        start = int(rng.uniform(0.0, 1.9) * RATE)
        hit = S.exp_decay(S.lowpass(S.noise(0.12, 100 + k), 2500), 0.03)
        for i, v in enumerate(hit):
            if start + i < len(track):
                track[start + i] += v * rng.uniform(0.3, 0.9)
    boom = S.exp_decay(S.osc(60, 1.5, "sine", freq_end=26), 0.3)
    crunch = track
    return S.fade_edges(S.normalize(S.mix(rumble, crunch, S.offset(boom, 0.05), gains=[1.0, 0.6, 0.9]), 0.92), 4)


def select_unit():
    return S.concat(_blip(520, 45, "square", tau=0.03, gain=0.25), _blip(780, 60, "square", tau=0.04, gain=0.25))


def move_unit():
    return S.concat(_blip(700, 40, "square", tau=0.03, gain=0.25), S.silence(0.02), _blip(700, 40, "square", tau=0.03, gain=0.25))


def attack_unit():
    return S.concat(_blip(400, 50, "saw", tau=0.04, gain=0.3), _blip(300, 90, "saw", tau=0.06, gain=0.3))


def select_building():
    return S.concat(_blip(330, 70, "tri", tau=0.05), _blip(440, 120, "tri", tau=0.08))


def unit_created():
    return S.concat(_blip(660, 60, "sine", tau=0.05), _blip(880, 60, "sine", tau=0.05), _blip(1100, 140, "sine", tau=0.09))


def construction_done():
    return S.concat(_blip(523, 90, "tri", tau=0.07), _blip(659, 90, "tri", tau=0.07), _blip(784, 90, "tri", tau=0.07),
                    _blip(1047, 260, "tri", tau=0.16))


def construction_loop():
    """Two hammer taps and a pause; loops while a structure goes up."""
    def tap(freq):
        return S.mix(S.exp_decay(S.highpass(S.noise(0.08, 51), 1500), 0.012), S.exp_decay(S.osc(freq, 0.09, "tri"), 0.02),
                     gains=[0.7, 0.6])
    return S.fade_edges(S.normalize(S.concat(tap(520), S.silence(0.16), tap(430), S.silence(0.46)), 0.6), 2)


def engine_loop():
    base = S.osc(55, 1.0, "saw")
    wob = [s * (0.8 + 0.2 * math.sin(2 * math.pi * 11 * i / RATE)) for i, s in enumerate(base)]
    return S.fade_edges(S.normalize(S.lowpass(wob, 260), 0.5), 30)


# name -> (maker, event definition lines (INI), kind). Event lines are the body of an AudioEvent.
def effects():
    return {
        "gui_click": gui_click, "gui_click_disabled": gui_click_disabled, "gui_command_click": gui_command_click,
        "gui_combo_click": gui_combo_click, "gui_message": gui_message, "gui_blip": gui_blip, "gui_type": gui_type,
        "money": money, "no_can_do": no_can_do, "radar_ping": radar_ping, "rifle_fire": rifle_fire,
        "rocket_fire": rocket_fire, "cannon_fire": cannon_fire, "explosion_small": explosion_small,
        "explosion_big": explosion_big, "building_collapse": building_collapse, "select_unit": select_unit,
        "move_unit": move_unit, "attack_unit": attack_unit, "select_building": select_building,
        "unit_created": unit_created, "construction_done": construction_done, "engine_loop": engine_loop,
        "construction_loop": construction_loop,
    }


# event name -> (wave names, extra INI lines)
UI = ("Type = UI EVERYONE", "Priority = NORMAL", "Volume = 80")
WORLD = ("Type = WORLD SHROUDED EVERYONE", "Priority = NORMAL", "MinRange = 40", "MaxRange = 500")
VOICE = ("Type = UI VOICE PLAYER EVERYONE", "Priority = HIGH", "Volume = 70", "Limit = 2")

EVENTS = [
    ("GUIClick", ["gui_click"], UI),
    ("GUIClickDisabled", ["gui_click_disabled"], UI),
    ("GUICommandBarClick", ["gui_command_click"], UI),
    ("GUIComboBoxClick", ["gui_combo_click"], UI),
    ("GUIBlip", ["gui_blip"], UI),
    ("GUIMessageReceived", ["gui_message"], UI),
    ("GUITypeText", ["gui_type"], UI + ("Limit = 1",)),
    ("StarterMoney", ["money"], UI),
    ("StarterNoCanDo", ["no_can_do"], UI),
    ("StarterRadarPing", ["radar_ping"], UI + ("Priority = HIGH",)),
    ("RadarEvent", ["radar_ping"], UI + ("Priority = HIGH",)),
    ("StarterRifleFire", ["rifle_fire"], WORLD + ("Volume = 60", "PitchShift = -10 10", "Limit = 6")),
    ("StarterRocketFire", ["rocket_fire"], WORLD + ("Volume = 75", "Limit = 4")),
    ("StarterCannonFire", ["cannon_fire"], WORLD + ("Volume = 90", "Limit = 4")),
    ("StarterExplosionSmall", ["explosion_small"], WORLD + ("Volume = 85", "PitchShift = -8 8", "Limit = 5")),
    ("StarterExplosionBig", ["explosion_big"], WORLD + ("Volume = 100", "Limit = 3")),
    ("StarterBuildingCollapse", ["building_collapse"], WORLD + ("Volume = 100", "Limit = 2", "MaxRange = 800")),
    ("StarterSelectUnit", ["select_unit"], VOICE),
    ("StarterMoveUnit", ["move_unit"], VOICE),
    ("StarterAttackUnit", ["attack_unit"], VOICE),
    ("StarterSelectBuilding", ["select_building"], VOICE),
    ("StarterUnitCreated", ["unit_created"], VOICE),
    ("StarterConstructionDone", ["construction_done"], UI + ("Priority = HIGH",)),
    ("StarterUnderConstruction", ["construction_loop"], WORLD + ("Volume = 45", "Control = LOOP", "LoopCount = 0", "Limit = 3")),
    # events the engine looks up by a fixed name (found by grepping the sources for AudioEventRTS literals); each reuses
    # one of the effects above so that no lookup ends in "No info for requested audio event"
    ("PlaceBuilding", ["select_building"], UI),
    ("RallyPointSet", ["gui_command_click"], UI),
    ("UnableToSetRallyPoint", ["no_can_do"], UI),
    ("NoCanDoSound", ["no_can_do"], UI),
    ("BeaconPlaced", ["gui_command_click"], UI),
    ("BeaconPlacementFailed", ["no_can_do"], UI),
    ("GUIBoarderFadeIn", ["gui_blip"], UI),
    ("GUIButtonsFadeIn", ["gui_blip"], UI),
    ("GUITransitionFade", ["gui_blip"], UI),
    ("GUICommunicatorIncoming", ["gui_message"], UI),
    ("GUICommunicatorOpen", ["gui_blip"], UI),
    ("GUILogoMouseOver", ["gui_blip"], UI),
    ("GUILogoSelect", ["gui_click"], UI),
    ("GUIScoreScreenPictures", ["gui_blip"], UI),
    ("GUIScoreScreenTick", ["gui_type"], UI + ("Limit = 1",)),
    ("MilitarySubtitlesTyping", ["gui_type"], UI + ("Limit = 1",)),
    ("LoadScreenAmbient", ["gui_blip"], UI),
]


# ------------------------------------------------------------------------------------------------- music

def _place(track, samples, start):
    end = start + len(samples)
    if end > len(track):
        track.extend([0.0] * (end - len(track)))
    for i, v in enumerate(samples):
        track[start + i] += v


def _kick():
    return S.exp_decay(S.osc(140, 0.22, "sine", freq_end=42), 0.07)


def _snare():
    return S.mix(S.exp_decay(S.highpass(S.noise(0.2, 33), 900), 0.05), S.exp_decay(S.osc(200, 0.12, "tri"), 0.03),
                 gains=[0.7, 0.5])


def _hat():
    return S.exp_decay(S.highpass(S.noise(0.06, 44), 5000), 0.012)


MINOR = [0, 2, 3, 5, 7, 8, 10]
MAJOR = [0, 2, 4, 5, 7, 9, 11]


def _chord(root, minor):
    return [root, root + (3 if minor else 4), root + 7]


def song(bpm, progression, scale, seed, drums="none", lead=True, bars_repeat=1, tail=1.5):
    """progression: list of (root midi, minor?) one per bar; returns float samples (mono)."""
    beat = 60.0 / bpm
    bar = beat * 4
    rng = Rng(seed)
    total = int((bar * len(progression) * bars_repeat + tail) * RATE)
    track = [0.0] * total
    for rep in range(bars_repeat):
        for b, (root, minor) in enumerate(progression):
            t0 = int((rep * len(progression) + b) * bar * RATE)
            notes = _chord(root, minor)
            # pad: slow attack triangle waves, one octave up from the root
            for k, n in enumerate(notes):
                tone = S.envelope(S.osc(S.note_freq(n + 12), bar + 0.4, "tri", phase=k * 0.17), attack=0.35, release=0.6)
                _place(track, S.scale(tone, 0.16), t0)
            # bass on beats 1 and 3 (and an off beat pick up)
            for q in (0, 2):
                tone = S.exp_decay(S.lowpass(S.osc(S.note_freq(root - 12), beat * 1.6, "saw"), 420), 0.5)
                _place(track, S.scale(S.fade_edges(tone, 3), 0.42), t0 + int(q * beat * RATE))
            if drums != "none":
                for q in range(4):
                    ts = t0 + int(q * beat * RATE)
                    if q in (0, 2) or drums == "battle":
                        _place(track, S.scale(_kick(), 0.6 if q == 0 else 0.4), ts)
                    if q in (1, 3) and drums == "battle":
                        _place(track, S.scale(_snare(), 0.35), ts)
                    for h in (0, 1) if drums == "battle" else (1,):
                        _place(track, S.scale(_hat(), 0.12), ts + int(h * beat * 0.5 * RATE))
            if lead:
                scale_notes = [root + 12 + s for s in scale] + [root + 24 + s for s in scale[:3]]
                step = 0
                for e in range(8):
                    if rng.random() < 0.35:
                        continue
                    step = max(0, min(len(scale_notes) - 1, step + rng.randint(-2, 3)))
                    n = scale_notes[step] if rng.random() < 0.8 else notes[rng.randint(0, 2)] + 24
                    tone = S.pluck(S.note_freq(n), beat * 1.1, seed=seed + b * 8 + e)
                    _place(track, S.scale(S.fade_edges(tone, 3), 0.26), t0 + int(e * beat * 0.5 * RATE))
    return S.normalize(S.fade_edges(track, 20), 0.75)


def music_tracks():
    """name -> (samples, loop?)"""
    return {
        # mellow minor theme for the menus
        "starter_menu": (song(84, [(57, True), (53, False), (48, False), (55, False)] * 2, MINOR, 5, drums="none",
                              tail=2.0), True),
        # driving battle theme
        "starter_battle": (song(118, [(57, True), (57, True), (53, False), (55, False)] * 2, MINOR, 9, drums="battle"), True),
        # load screen: short and rising
        "starter_load": (song(96, [(52, True), (55, False), (57, True), (59, False)], MINOR, 13, drums="soft", tail=2.5),
                         False),
        # score screen: resolves to a major chord
        "starter_score": (song(80, [(53, False), (55, False), (57, True), (60, False)], MAJOR, 17, drums="none", lead=True,
                               tail=3.0), False),
    }


MUSIC_EVENTS = [
    # event name, file, loop
    ("StarterMenuMusic", "starter_menu.wav", True),
    ("StarterBattleMusic", "starter_battle.wav", True),
    ("StarterLoadMusic", "starter_load.wav", False),
    ("StarterScoreMusic", "starter_score.wav", False),
    # played while the credits scroll (CreditsMenuInit asks for the track "Credits")
    ("Credits", "starter_menu.wav", True),
]


# ------------------------------------------------------------------------------------------------- INI text

def audio_settings_ini():
    return """; Starter pack audio configuration. GPL-3.0-or-later, original work.
; Files: <AudioRoot>\\<SoundsFolder|MusicFolder>\\name[.SoundsExtension] (AudioEventRTS::generateFilenamePrefix).
AudioSettings
  AudioRoot = Data\\Audio
  SoundsFolder = Sounds
  MusicFolder = Tracks
  StreamingFolder = Speech
  SoundsExtension = wav
  UseDigital = Yes
  UseMidi = No
  OutputRate = 22050
  OutputBits = 16
  OutputChannels = 2
  SampleCount2D = 16
  SampleCount3D = 24
  StreamCount = 4
  GlobalMinRange = 50
  GlobalMaxRange = 600
  TimeBetweenDrawableSounds = 5000
  TimeToFadeAudio = 600
  AudioFootprintInBytes = 16000000
  MinSampleVolume = 3%
  Relative2DVolume = 80%
  DefaultSoundVolume = 80%
  Default3DSoundVolume = 80%
  DefaultSpeechVolume = 70%
  DefaultMusicVolume = 60%
  DefaultMoneyTransactionVolume = 60%
  MicrophoneDesiredHeightAboveTerrain = 100.0
  MicrophoneMaxPercentageBetweenGroundAndCamera = 80%
  ZoomMinDistance = 100.0
  ZoomMaxDistance = 400.0
  ZoomSoundVolumePercentageAmount = 20%
End
"""


def default_sound_ini():
    return """; Starter pack: the defaults every AudioEvent starts from. GPL-3.0-or-later, original work.
AudioEvent DefaultSoundEffect
  Volume = 100
  VolumeShift = 0
  MinVolume = 0
  PitchShift = 0 0
  Delay = 0 0
  Limit = 0
  LoopCount = 1
  Priority = NORMAL
  Type = WORLD SHROUDED
  MinRange = 50.0
  MaxRange = 300.0
  LowPassCutoff = 100%
End
"""


def default_music_ini():
    return """; Starter pack: the defaults every MusicTrack starts from. GPL-3.0-or-later, original work.
MusicTrack DefaultMusicTrack
  Volume = 100
  VolumeShift = 0
  MinVolume = 0
  Delay = 0 0
  Limit = 0
  LoopCount = 1
  Priority = NORMAL
End
"""


def sound_effects_ini():
    out = ["; Starter pack sound effects, generated by gen/audio.py. GPL-3.0-or-later, original work.", ""]
    for name, waves, lines in EVENTS:
        out.append("AudioEvent %s" % name)
        out.append("  Sounds = %s" % " ".join(waves))
        for line in lines:
            out.append("  " + line)
        out.append("End")
        out.append("")
    out.append("; looping engine hum (not used by an object unless wanted)")
    out.append("AudioEvent StarterEngineLoop")
    out.append("  Sounds = engine_loop")
    for line in WORLD + ("Volume = 40", "Control = LOOP", "LoopCount = 0"):
        out.append("  " + line)
    out.append("End")
    return "\n".join(out) + "\n"


def music_ini():
    out = ["; Starter pack music, generated by gen/audio.py. GPL-3.0-or-later, original work.", ""]
    for name, filename, loop in MUSIC_EVENTS:
        out.append("MusicTrack %s" % name)
        out.append("  Filename = %s" % filename)
        out.append("  Volume = 100")
        if loop:
            out.append("  Control = LOOP")
            out.append("  LoopCount = 0")
        out.append("End")
        out.append("")
    return "\n".join(out)


def misc_audio_ini():
    return """; Starter pack: sounds the code plays by role. GPL-3.0-or-later, original work.
MiscAudio
  RadarNotifyUnitUnderAttackSound = StarterRadarPing
  RadarNotifyHarvesterUnderAttackSound = StarterRadarPing
  RadarNotifyStructureUnderAttackSound = StarterRadarPing
  RadarNotifyUnderAttackSound = StarterRadarPing
  RadarNotifyOnlineSound = GUIBlip
  RadarNotifyOfflineSound = GUIClickDisabled
  GUIClickSound = GUIClick
  NoCanDoSound = StarterNoCanDo
  MoneyDepositSound = StarterMoney
  MoneyWithdrawSound = GUIBlip
  BuildingDisabled = GUIClickDisabled
  BuildingReenabled = GUIBlip
  VehicleDisabled = GUIClickDisabled
  VehicleReenabled = GUIBlip
  UnitPromoted = StarterConstructionDone
End
"""


# ------------------------------------------------------------------------------------------------- output

def generate(emit):
    emit("Data/INI/AudioSettings.ini", audio_settings_ini())
    emit("Data/INI/Default/SoundEffects.ini", default_sound_ini())
    emit("Data/INI/Default/Music.ini", default_music_ini())
    emit("Data/INI/SoundEffects.ini", sound_effects_ini())
    emit("Data/INI/Music.ini", music_ini())
    emit("Data/INI/MiscAudio.ini", misc_audio_ini())
    for name, make in sorted(effects().items()):
        emit("Data/Audio/Sounds/%s.wav" % name, write_wav(make(), RATE))
    for name, (samples, _loop) in sorted(music_tracks().items()):
        emit("Data/Audio/Tracks/%s.wav" % name, write_ima_adpcm(samples, RATE))
