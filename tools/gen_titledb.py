#!/usr/bin/env python3
# SPDX-FileCopyrightText: Azahar Emulator Project
# Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
# SPDX-License-Identifier: GPL-2.0-or-later

"""Regenerates src/citra_switch/titledb.bin from GameTDB's 3dstdb.xml.

Extracts every field GameTDB has for each title (developer, publisher, genre,
release date, region, languages, rating, Wi-Fi/input player counts, and the
synopsis in every UI language this app supports) into a compact, sorted binary
lookup keyed by the title's 4-character GameTDB game ID, so the emulator can
look up everything about a title with a single O(log n) binary search and no
XML/network involved at runtime.

Download 3dstdb.xml from https://www.gametdb.com/3DS/Downloads (or via
GameTDB's site directly), then run:

    ./tools/gen_titledb.py path/to/3dstdb.xml

Binary format (little-endian, see titledb.cpp's GameRecord for the C++ side):
    magic "TDB3" (4 bytes)
    u32 count
    count * GameRecord (fixed 178 bytes each, packed, sorted ascending by id):
        char id[4]
        u16 release_year
        u8 release_month
        u8 release_day
        u8 wifi_players
        u8 input_players
        9 * { u32 text_offset; u32 text_length }, in this order:
            developer, publisher, genre, region, languages,
            rating_type, rating_value, rating_descriptors, wifi_features
        12 * { u32 text_offset; u32 text_length }, one per LANG_ORDER entry:
            synopsis in that language
    UTF-8 text blob, referenced by the (offset, length) pairs above
"""

import os
import struct
import sys
import xml.etree.ElementTree as ET

DEFAULT_DST = os.path.join(os.path.dirname(__file__), "..", "src", "citra_switch", "titledb.bin")

STRING_FIELDS = [
    "developer", "publisher", "genre", "region", "languages",
    "rating_type", "rating_value", "rating_descriptors", "wifi_features",
]

LANG_ORDER = ["JA", "EN", "FR", "DE", "IT", "ES", "ZHCN", "KO", "NL", "PT", "RU", "ZHTW"]


def clean_multiline(text: str) -> str:
    """Collapses CRLF and repeated blank lines - the on-device renderer is a plain
    word-wrapping text draw, no benefit to preserving the source's exact whitespace."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = [line.strip() for line in text.split("\n")]
    collapsed = []
    prev_blank = True
    for line in lines:
        blank = line == ""
        if blank and prev_blank:
            continue
        collapsed.append(line)
        prev_blank = blank
    return "\n".join(collapsed).strip()


def extract(game: ET.Element) -> dict:
    fields = {name: "" for name in STRING_FIELDS}
    fields["synopsis"] = ["" for _ in LANG_ORDER]

    developer_el = game.find("developer")
    if developer_el is not None and developer_el.text:
        fields["developer"] = developer_el.text.strip()

    publisher_el = game.find("publisher")
    if publisher_el is not None and publisher_el.text:
        fields["publisher"] = publisher_el.text.strip()

    genre_el = game.find("genre")
    if genre_el is not None and genre_el.text:
        fields["genre"] = genre_el.text.strip()

    region_el = game.find("region")
    if region_el is not None and region_el.text:
        fields["region"] = region_el.text.strip()

    languages_el = game.find("languages")
    if languages_el is not None and languages_el.text:
        fields["languages"] = languages_el.text.strip()

    rating_el = game.find("rating")
    year = month = day = 0
    if rating_el is not None:
        fields["rating_type"] = rating_el.get("type", "") or ""
        fields["rating_value"] = rating_el.get("value", "") or ""
        descriptors = [d.text.strip() for d in rating_el.findall("descriptor")
                      if d.text and d.text.strip()]
        fields["rating_descriptors"] = ", ".join(descriptors)

    date_el = game.find("date")
    if date_el is not None:
        try:
            year = int(date_el.get("year", "0") or "0")
            month = int(date_el.get("month", "0") or "0")
            day = int(date_el.get("day", "0") or "0")
        except ValueError:
            year = month = day = 0

    wifi_el = game.find("wi-fi")
    wifi_players = 0
    if wifi_el is not None:
        try:
            wifi_players = int(wifi_el.get("players", "0") or "0")
        except ValueError:
            wifi_players = 0
        features = [f.text.strip() for f in wifi_el.findall("feature")
                   if f.text and f.text.strip()]
        fields["wifi_features"] = ", ".join(features)

    input_el = game.find("input")
    input_players = 0
    if input_el is not None:
        try:
            input_players = int(input_el.get("players", "0") or "0")
        except ValueError:
            input_players = 0

    for locale in game.findall("locale"):
        lang = locale.get("lang")
        if lang not in LANG_ORDER:
            continue
        syn_el = locale.find("synopsis")
        if syn_el is not None and syn_el.text and syn_el.text.strip():
            fields["synopsis"][LANG_ORDER.index(lang)] = clean_multiline(syn_el.text.strip())

    fields["_year"] = min(max(year, 0), 0xFFFF)
    fields["_month"] = min(max(month, 0), 0xFF)
    fields["_day"] = min(max(day, 0), 0xFF)
    fields["_wifi_players"] = min(max(wifi_players, 0), 0xFF)
    fields["_input_players"] = min(max(input_players, 0), 0xFF)
    return fields


def main() -> None:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} path/to/3dstdb.xml [output.bin]", file=sys.stderr)
        sys.exit(1)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_DST

    root = ET.parse(src).getroot()

    entries: dict[str, dict] = {}
    skipped_no_id = 0
    skipped_bad_id = 0
    total_games = 0

    for game in root.findall("game"):
        total_games += 1
        id_el = game.find("id")
        if id_el is None or not id_el.text:
            skipped_no_id += 1
            continue
        game_id = id_el.text.strip()
        if len(game_id) != 4 or not game_id.isascii():
            skipped_bad_id += 1
            continue

        # Last entry with a given id wins - ids are meant to be unique per release, but this
        # keeps the output deterministic even if the source ever has a duplicate.
        entries[game_id] = extract(game)

    print(f"Total <game> entries: {total_games}")
    print(f"Skipped (no id): {skipped_no_id}")
    print(f"Skipped (bad id format): {skipped_bad_id}")
    print(f"Kept: {len(entries)}")

    sorted_ids = sorted(entries.keys())

    index_bytes = bytearray()
    blob = bytearray()

    def push_string(text: str) -> tuple:
        data = text.encode("utf-8")
        offset = len(blob)
        blob.extend(data)
        return offset, len(data)

    for game_id in sorted_ids:
        fields = entries[game_id]
        index_bytes.extend(game_id.encode("ascii"))
        index_bytes.extend(struct.pack("<HBBBB", fields["_year"], fields["_month"],
                                       fields["_day"], fields["_wifi_players"],
                                       fields["_input_players"]))
        for name in STRING_FIELDS:
            offset, length = push_string(fields[name])
            index_bytes.extend(struct.pack("<II", offset, length))
        for synopsis_text in fields["synopsis"]:
            offset, length = push_string(synopsis_text)
            index_bytes.extend(struct.pack("<II", offset, length))

    with open(dst, "wb") as f:
        f.write(b"TDB3")
        f.write(struct.pack("<I", len(sorted_ids)))
        f.write(index_bytes)
        f.write(blob)

    print(f"Wrote {dst}: {os.path.getsize(dst)} bytes "
          f"({len(sorted_ids)} entries, index={len(index_bytes)}B, text={len(blob)}B)")


if __name__ == "__main__":
    main()
