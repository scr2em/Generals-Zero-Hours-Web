"""Writes the string table from every label registered through ``textdb`` (runs last in the build)."""
from . import textdb
from .strings_data import add_all
from spk.strfile import StringTable


def generate(emit):
    add_all()
    table = StringTable()
    for label, english in sorted(textdb.table().items(), key=lambda kv: kv[0].lower()):
        table.add(label, english)
    emit("Data/Generals.str", table.to_str())
