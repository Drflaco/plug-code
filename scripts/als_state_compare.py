# Compare l'état PlugState sauvegardé dans un Live Set (.als) avec un fichier
# d'état XML de référence (rappel de session à l'identique, CdC §3.6, J3).
# Compare tout hors PARAM (les PARAM sont comparés à part, valeur par valeur).
# Usage : python scripts/als_state_compare.py set.als reference.xml
import gzip, re, sys
import xml.etree.ElementTree as ET

als, ref = sys.argv[1], sys.argv[2]
root = ET.fromstring(gzip.open(als).read())

def plug_states():
    for dev in root.iter("PluginDevice"):
        v3 = dev.find("PluginDesc/Vst3PluginInfo")
        if v3 is None or v3.find("Name").get("Value") != "Plug":
            continue
        ps = next(dev.iter("ProcessorState"))
        raw = bytes.fromhex(re.sub(r"\s+", "", ps.text or ""))
        i, j = raw.find(b"<PlugState"), raw.find(b"</PlugState>")
        yield ET.fromstring(raw[i:j + len(b"</PlugState>")].decode("utf-8"))

def canon(el):
    """Forme canonique : (tag, attributs triés, enfants canoniques), PARAM exclus."""
    kids = [canon(c) for c in el if c.tag != "PARAM"]
    attrs = tuple(sorted((k, norm(v)) for k, v in el.attrib.items() if k != "pluginVersion"))
    return (el.tag, attrs, tuple(kids))

def norm(v):
    try:
        f = float(v)
        return repr(round(f, 6))
    except ValueError:
        return v

def params(el):
    return {p.get("id"): norm(p.get("value")) for p in el.iter("PARAM")}

reference = ET.parse(ref).getroot()
ok_any = False
for n, st in enumerate(plug_states(), 1):
    same_tree = canon(st) == canon(reference)
    pa, pb = params(st), params(reference)
    diff = {k: (pa.get(k), pb.get(k)) for k in set(pa) | set(pb) if pa.get(k) != pb.get(k)}
    print(f"instance {n} : schemaVersion={st.get('schemaVersion')} arbre hors PARAM {'IDENTIQUE' if same_tree else 'DIFFÉRENT'} ; PARAM différents : {len(diff)} {list(diff.items())[:5]}")
    ok_any |= same_tree and not diff
print("RÉSULTAT :", "RAPPEL À L'IDENTIQUE" if ok_any else "ÉCART")
sys.exit(0 if ok_any else 1)
