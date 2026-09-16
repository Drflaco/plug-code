"""Convertit un preset .plugstate (notre format, XML lisible) en .vstpreset
(format de presets VST3, celui que les hôtes savent charger). Même état, deux
formats : le .plugstate reste la source de vérité versionnée du dépôt.

Le format a été relevé sur un fichier écrit par Live 12.4 lui-même le 16/09 :
  "VST3" | version int32 | identifiant de classe, 32 caractères | offset de la
  table int64 | données | table "List" des morceaux.
Le morceau du composant est ce que JUCE produit dans getStateInformation :
le nombre magique "VC2!", la longueur du XML, le XML, un octet nul.

Usage : python scripts/plugstate_to_vstpreset.py presets/*.plugstate
"""

import struct
import sys
from pathlib import Path

# Identifiant de la classe audio, lu dans Contents/Resources/moduleinfo.json du
# bundle. Il dérive de l'identité VST3 figée (Lascaux Lab / Lscx / Plug) : le
# changer reviendrait à changer de plugin aux yeux de l'hôte.
COMPONENT_CID = "ABCDEF019182FAEB4C736378506C7567"

MAGIC_XML = b"VC2!"          # juce::AudioProcessor::copyXmlToBinary


def juce_state_chunk(xml_text: str) -> bytes:
    """Reproduit octet pour octet ce que copyXmlToBinary écrit."""
    payload = xml_text.encode("utf-8")
    return MAGIC_XML + struct.pack("<I", len(payload)) + payload + b"\x00"


def build_vstpreset(xml_text: str) -> bytes:
    comp = juce_state_chunk(xml_text)
    cont = b""                      # état du contrôleur : vide, il se déduit du composant

    header_size = 4 + 4 + 32 + 8    # "VST3", version, identifiant, offset de la table
    comp_offset = header_size
    cont_offset = comp_offset + len(comp)
    list_offset = cont_offset + len(cont)

    out = bytearray()
    out += b"VST3"
    out += struct.pack("<I", 1)
    out += COMPONENT_CID.encode("ascii")
    out += struct.pack("<q", list_offset)
    out += comp
    out += cont

    out += b"List"
    out += struct.pack("<i", 2)
    out += b"Comp" + struct.pack("<qq", comp_offset, len(comp))
    out += b"Cont" + struct.pack("<qq", cont_offset, len(cont))
    return bytes(out)


def main(argv):
    targets = [Path(a) for a in argv[1:]]
    if not targets:
        targets = sorted(Path("presets").glob("*.plugstate"))
    if not targets:
        print("aucun .plugstate a convertir")
        return 2

    for src in targets:
        xml_text = src.read_text(encoding="utf-8")
        if "<PlugState" not in xml_text:
            print(f"  ignore (pas un etat Plug) : {src}")
            continue
        dst = src.with_suffix(".vstpreset")
        dst.write_bytes(build_vstpreset(xml_text))
        print(f"  {dst.name}  ({dst.stat().st_size} octets)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
