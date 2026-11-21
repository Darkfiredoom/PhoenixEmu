#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds dist/amiibo_table.json for the Amiibo Library: amiibo ID -> [name, amiipedia image, series].

Names and IDs come from the AmiiboAPI database; amiipedia lists no IDs, so its pictures are matched
by series and name. Run it again when new amiibo come out:

    python3 -I tools/amiibo/make_amiibo_table.py
"""

import json
import pathlib
import re
import sys
import unicodedata
import urllib.request

AMIIBOAPI = "https://raw.githubusercontent.com/N3evin/AmiiboAPI/master/database/amiibo.json"
AMIIPEDIA = "https://amiipedia.com/amiibo-database"
OUT = pathlib.Path(__file__).resolve().parents[2] / "dist" / "amiibo_table.json"
STOP = {"the", "of", "and", "s", "amiibo"}


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "PhoenixEmu amiibo table"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read().decode("utf-8")


def slug(text):
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode().lower()
    text = re.sub(r"['.]", "", text.replace("&", " and "))
    return re.sub(r"[^a-z0-9]+", "-", text).strip("-")


def tokens(text):
    return {t for t in slug(text).split("-") if t and t not in STOP}


def amiipedia_entries():
    # The database page embeds its search index: name, slug, picture and series of every amiibo.
    html = fetch(AMIIPEDIA).replace('\\"', '"')
    pattern = r'\{"k":"a","n":"([^"]*)","s":"([^"]*)","i":"([^"]*)","c":"([^"]*)"\}'
    entries = []
    for name, _, image, series in re.findall(pattern, html):
        entries.append({
            "name": name,
            "image": image.rsplit("/", 1)[-1],
            "name_tokens": tokens(name),
            "series_tokens": tokens(series),
        })
    return entries


def best_match(name, series, kind, entries):
    name_tokens = tokens(name)
    series_tokens = tokens(series)
    best, best_score, ties = None, 0.0, 0
    for entry in entries:
        if series != "Others" and not series_tokens & entry["series_tokens"]:
            continue
        # Animal Crossing has a figure and several cards of the same villagers.
        if "animal" in series_tokens and (kind == "Card") != ("cards" in entry["series_tokens"]):
            continue
        if slug(entry["name"]) == slug(name):
            score = 2.0
        else:
            shared = len(name_tokens & entry["name_tokens"])
            if shared == 0:
                continue
            score = shared / len(name_tokens | entry["name_tokens"])
            if name_tokens <= entry["name_tokens"] or entry["name_tokens"] <= name_tokens:
                score += 0.5
        # Series share words ("Super Mario Bros." and "Super Smash Bros."): the closer one wins.
        series_union = series_tokens | entry["series_tokens"]
        if series_union:
            score += 0.4 * len(series_tokens & entry["series_tokens"]) / len(series_union)
        if score > best_score:
            best, best_score, ties = entry, score, 1
        elif score == best_score:
            ties += 1
    return (best, ties) if best_score >= 0.6 else (None, 0)


def main():
    database = json.loads(fetch(AMIIBOAPI))
    entries = amiipedia_entries()
    if not entries:
        sys.exit("amiipedia's search index was not found; the page layout changed")

    table, pictures, ties = {}, 0, 0
    for amiibo_id, info in database["amiibos"].items():
        hex_id = amiibo_id[2:].lower()
        series = database["amiibo_series"].get("0x" + hex_id[12:14], "")
        kind = database["types"].get("0x" + hex_id[6:8], "")
        entry, count = best_match(info["name"], series, kind, entries)
        table[hex_id] = [info["name"], entry["image"] if entry else "", series]
        pictures += entry is not None
        ties += count > 1

    OUT.write_text(json.dumps(dict(sorted(table.items())), ensure_ascii=False, indent=0) + "\n",
                   encoding="utf-8")
    print(f"{len(table)} amiibo, {pictures} with pictures ({ties} picked among equal matches) "
          f"-> {OUT}")


if __name__ == "__main__":
    main()
