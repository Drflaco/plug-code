# Inspecte un Live Set (.als = XML gzippé) : instances VST3 « Plug », leur état
# JUCE (valeurs de la grille), les paramètres configurés côté Live et les
# enveloppes d'automation associées.
# Usage : python scripts/als_inspect.py <fichier.als> [id1 id2 ...]
import gzip
import re
import sys
import zlib
import xml.etree.ElementTree as ET

path = sys.argv[1]
wanted = sys.argv[2:] or ["macro1", "master.drive", "slot01.main", "master.quality", "master.mixLaw"]
root = ET.fromstring(gzip.open(path).read())

GZIP_MAGIC = bytes([0x1F, 0x8B, 0x08])


def juce_state_from_buffer(hex_text):
    """copyXmlToBinary (JUCE) : magic "VC2!", taille int32, puis XML UTF-8 en clair."""
    raw = bytes.fromhex(re.sub(r"\s+", "", hex_text))
    i = raw.find(b"<PlugState")
    j = raw.find(b"</PlugState>")
    if i < 0 or j < 0:
        return None
    return raw[i:j + len(b"</PlugState>")].decode("utf-8", "replace")


for dev in root.iter("PluginDevice"):
    desc = dev.find("PluginDesc")
    v3 = desc.find("Vst3PluginInfo") if desc is not None else None
    if v3 is None or v3.find("Name").get("Value") != "Plug":
        continue
    print("== instance VST3 'Plug' ==")
    buf = next((b for b in dev.iter("ProcessorState")), None)
    state = juce_state_from_buffer(buf.text or "") if buf is not None else None
    if state:
        st = ET.fromstring(state)
        params = {p.get("id"): p.get("value") for p in st.iter("PARAM")}
        print("  schemaVersion :", st.get("schemaVersion"), "| PARAM sauvegardés :", len(params))
        for w in wanted:
            print(f"    {w} = {params.get(w)}")
    else:
        print("  (état JUCE introuvable dans le buffer)")
    for pf in dev.iter("PluginFloatParameter"):
        pname = pf.find("ParameterName").get("Value")
        if not pname:
            continue
        val = pf.find("ParameterValue/Manual").get("Value")
        tgt = pf.find("ParameterValue/AutomationTarget")
        print(f"  Live param configuré : {pname} = {val} (AutomationTarget Id={tgt.get('Id') if tgt is not None else None})")

for env in root.iter("AutomationEnvelope"):
    tgt = env.find("EnvelopeTarget/PointeeId").get("Value")
    events = env.findall("Automation/Events/FloatEvent")
    if events:
        vals = [e.get("Value") for e in events]
        print(f"AutomationEnvelope cible {tgt} : {len(events)} événements, valeurs {vals[:3]}..{vals[-2:]}")
