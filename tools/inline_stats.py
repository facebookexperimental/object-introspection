#
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
import atexit
import gc
import json
import re
import signal
import sqlite3
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import click
from elftools.common.py3compat import bytes2str
from elftools.elf.elffile import ELFFile

GC_COUNTER_INIT = 0
EXCLUDE_PATTERN = None
DB = None

# README:
# This script depends on the python library pyelftools: https://github.com/eliben/pyelftools
# Please make sure you have it installed before trying it out.

# For internal use, please see: https://fburl.com/code/1f6a4vjj


@atexit.register
def close_db():
    """
    Ensure that the database is properly closed before exiting
    """
    if DB:
        DB.commit()
        DB.close()


@click.command(context_settings={"help_option_names": ["-h", "--help"]})
@click.option(
    "--gc",
    type=int,
    default=10_000,
    help="After how many CU memory is manually freed. Low values reduce performance. High values can cause OOM.",
)
@click.option(
    "-o",
    "--output",
    type=click.Path(),
    default="instats.db",
    help="Name of the output SQLite3 database",
)
@click.option(
    "-c/ ",
    "--clear-db/--no-clear-db",
    default=False,
    help="Delete the database content on start",
)
@click.option(
    "-e",
    "--exclude",
    default=r".^",  # This pattern matches nothing, so no exclude by default
    help="Exclude CUs whose path match the given pattern",
)
@click.option(
    "-E",
    "--exclude-file",
    default=r".^",  # This pattern matches nothing, so no exclude by default
    help="Exclude files whose path match the given pattern",
)
@click.option(
    "-S/ ",
    "--follow-shared/--no-follow-shared",
    default=False,
    help="Traverse the shared library linked against the INPUTS",
)
@click.argument(
    "inputs", nargs=-1, type=click.Path(exists=True)
)  # help="Executable file to analyse"
def cli(gc, output, clear_db, exclude, exclude_file, follow_shared, inputs):
    """
    Traverses the DWARF data of the INPUTS and count how many arguments have location info.
    It counts for both function calls and inlined functions.
    The results are stored in a SQLite3 database for convenient data analysis.
    """

    global GC_COUNTER_INIT
    GC_COUNTER_INIT = gc

    global EXCLUDE_PATTERN
    EXCLUDE_PATTERN = re.compile(exclude)

    exclude_file_pattern = re.compile(exclude_file)
    ldd_pattern = re.compile(r".* => (.*) \(.*\)")

    create_db(output, clear_db)

    inputs = list(inputs)
    processed_files = set()
    for filename in inputs:
        if exclude_file_pattern.search(filename) or filename in processed_files:
            continue

        if follow_shared:
            ldd = subprocess.run(["ldd", filename], capture_output=True, text=True)
            shared_libraries = ldd_pattern.findall(ldd.stdout)
            inputs.extend(shared_libraries)

        processed_files.add(filename)
        process_file(filename)


def create_db(dbfilename, clear_db=False):
    global DB
    DB = sqlite3.connect(dbfilename)
    DB.executescript(
        """
        CREATE TABLE IF NOT EXISTS call_sites (
            function_name TEXT PRIMARY KEY,
            count INT DEFAULT 1
        );

        CREATE TABLE IF NOT EXISTS call_site_arguments (
            function_name TEXT NOT NULL REFERENCES call_sites(function_name),
            argument_name TEXT NOT NULL,
            count INT DEFAULT 1,
            loc_count INT DEFAULT 0,
            PRIMARY KEY (function_name, argument_name)
        );

        CREATE TABLE IF NOT EXISTS inlined_subprograms (
            function_name TEXT PRIMARY KEY,
            count INT DEFAULT 1
        );

        CREATE TABLE IF NOT EXISTS inlined_subprogram_arguments (
            function_name TEXT NOT NULL REFERENCES inlined_subprograms(function_name),
            argument_name TEXT NOT NULL,
            count INT DEFAULT 1,
            loc_count INT DEFAULT 0,
            PRIMARY KEY (function_name, argument_name)
        );
    """
    )

    if clear_db:
        DB.executescript(
            """
            DELETE FROM inlined_subprogram_arguments;
            DELETE FROM inlined_subprograms;
            DELETE FROM call_site_arguments;
            DELETE FROM call_sites;
        """
        )

    return DB


def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def process_file(filename):
    eprint("Processing file: ", filename)
    with open(filename, "rb") as f:
        elffile = ELFFile(f)

        if not elffile.has_dwarf_info():
            eprint("  file has no DWARF info")
            return

        dwarfinfo = elffile.get_dwarf_info()

        gc_counter = GC_COUNTER_INIT
        for CU in dwarfinfo._parse_CUs_iter():
            top_DIE = CU.get_top_DIE()
            top_DIE_path = Path(top_DIE.get_full_path()).as_posix()
            if EXCLUDE_PATTERN.search(top_DIE_path):
                eprint("Skipping", top_DIE_path, top_DIE.tag, top_DIE.offset)
                continue
            else:
                eprint(top_DIE_path, top_DIE.tag, top_DIE.offset, CU.cu_offset)

            process_die_rec(CU.get_top_DIE())

            # Clear cache when counter reaches 0, in an attempt to limit memory usage
            gc_counter -= 1
            if gc_counter <= 0:
                gc_counter = GC_COUNTER_INIT
                sys.stderr.flush()
                DB.commit()
                dwarfinfo._cu_offsets_map.clear()
                dwarfinfo._cu_cache.clear()
                gc.collect()


def process_die_rec(die):
    # Start by listing all subroutines we find
    if die.tag == "DW_TAG_subprogram":
        process_subprogram(die)
    else:
        for child in die.iter_children():
            process_die_rec(child)


def process_subprogram(die):
    # We can have subprograms that contains only a typedef
    # This might occur when the typedef is within a subprogram
    # What's weird is I haven't seen a reference to the original subprogram
    if not "DW_AT_name" in die.attributes:
        return

    # eprint("Subprogram: ", get_name(die))
    for child in die.iter_children():
        process_subprogram_rec(child)


def process_subprogram_rec(die):
    # Look for DW_TAG_inlined_subroutine and DW_TAG_GNU_call_site
    if die.tag == "DW_TAG_inlined_subroutine":
        process_inlined_subroutine(die)
    elif die.tag == "DW_TAG_GNU_call_site":
        process_call_site(die)
    else:
        for child in die.iter_children():
            process_subprogram_rec(child)


def collect_arguments(function_name, function_die, table):
    """
    Insert in the database all the arguments of the given function.
    We might not know that some argument are missing if they never appear in
    an inlined subprogram or call site. By collecting all the arguments, we
    make sure we are aware of this blind spot.
    """
    for child in function_die.iter_children():
        aname = None
        if child.tag == "DW_TAG_formal_parameter":
            aname = get_name(child)
        elif child.tag == "DW_TAG_GNU_call_site_parameter":
            aname = get_name(child)
        else:
            continue

        DB.execute(
            f"INSERT INTO {table} VALUES (?, ?, 0, 0) ON CONFLICT(function_name, argument_name) DO NOTHING",
            (function_name, aname),
        )


def process_inlined_subroutine(die):
    fname = get_name(die)
    DB.execute(
        """
        INSERT INTO inlined_subprograms VALUES (?, 1)
        ON CONFLICT(function_name) DO
            UPDATE SET count = count + 1
    """,
        (fname,),
    )

    if "DW_AT_abstract_origin" in die.attributes:
        collect_arguments(
            fname,
            die.get_DIE_from_attribute("DW_AT_abstract_origin"),
            "inlined_subprogram_arguments",
        )

    for child in die.iter_children():
        if child.tag == "DW_TAG_inlined_subroutine":
            process_inlined_subroutine(child)
        elif child.tag == "DW_TAG_formal_parameter":
            aname = get_name(child)
            DB.execute(
                """
                INSERT INTO inlined_subprogram_arguments VALUES (?, ?, 1, ?)
                ON CONFLICT(function_name, argument_name) DO UPDATE SET
                    count = count + 1,
                    loc_count = loc_count + excluded.loc_count
            """,
                (fname, aname, int("DW_AT_location" in child.attributes)),
            )


def process_call_site(die):
    fname = get_name(die)

    DB.execute(
        """
        INSERT INTO call_sites VALUES (?, 1)
        ON CONFLICT(function_name) DO
            UPDATE SET count = count + 1
    """,
        (fname,),
    )

    if "DW_AT_abstract_origin" in die.attributes:
        collect_arguments(
            fname,
            die.get_DIE_from_attribute("DW_AT_abstract_origin"),
            "call_site_arguments",
        )

    for child in die.iter_children():
        if child.tag == "DW_TAG_inlined_subroutine":
            process_inlined_subroutine(child)
        elif child.tag == "DW_TAG_formal_parameter":
            aname = get_name(child)
            DB.execute(
                """
                INSERT INTO call_site_arguments VALUES (?, ?, 1, ?)
                ON CONFLICT(function_name, argument_name) DO UPDATE SET
                    count = count + 1,
                    loc_count = loc_count + excluded.loc_count
            """,
                (fname, aname, int("DW_AT_location" in child.attributes)),
            )
        elif child.tag == "DW_TAG_GNU_call_site_parameter":
            aname = get_name(child)
            DB.execute(
                """
                INSERT INTO call_site_arguments VALUES (?, ?, 1, ?)
                ON CONFLICT(function_name, argument_name) DO UPDATE SET
                    count = count + 1,
                    loc_count = loc_count + excluded.loc_count
            """,
                (fname, aname, int("DW_AT_location" in child.attributes)),
            )


def print_die_attributes(die):
    for name, attr in die.attributes.items():
        eprint("  %s = %s" % (name, attr.value))


def get_name(die):
    # We work mainly with mangled C++ name, so look for the linkage name first
    if "DW_AT_linkage_name" in die.attributes:
        return bytes2str(die.attributes["DW_AT_linkage_name"].value)
    elif "DW_AT_name" in die.attributes:
        return bytes2str(die.attributes["DW_AT_name"].value)
    elif "DW_AT_abstract_origin" in die.attributes:
        return get_name(die.get_DIE_from_attribute("DW_AT_abstract_origin"))
    elif "DW_AT_specification" in die.attributes:
        return get_name(die.get_DIE_from_attribute("DW_AT_specification"))
    elif "DW_AT_type" in die.attributes:
        return get_name(die.get_DIE_from_attribute("DW_AT_type"))
    else:
        # print_die_attributes(die)
        return "<unknown>"


if __name__ == "__main__":
    cli()
