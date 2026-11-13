#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make G_DEFINE_TYPE / G_DEFINE_INTERFACE wasm-safe.

wasm traps ("function signature mismatch") when a function pointer is called
with another signature. The GLib macros register one-argument class, instance
and interface init functions that GLib calls with two arguments. Generate
two-argument functions instead. Usage: patch-gtype.py glib-src/gobject/gtype.h
"""
import sys

path = sys.argv[1]
s = open(path).read()
gobject_c = path.replace("gtype.h", "gobject.c")


def sub(old, new):
    global s
    if old not in s:
        sys.exit("patch-gtype: pattern not found: " + old)
    s = s.replace(old, new)


sub("static void     type_name##_class_intern_init (gpointer klass) \\",
    "static void     type_name##_class_intern_init (gpointer klass, gpointer class_data G_GNUC_UNUSED) \\")
sub("static gint     TypeName##_private_offset; \\\n",
    "static gint     TypeName##_private_offset; \\\n"
    "G_GNUC_UNUSED static void type_name##_init_adapter (GTypeInstance *self, gpointer g_class G_GNUC_UNUSED) \\\n"
    "{ type_name##_init ((TypeName *) self); } \\\n")
sub("(GInstanceInitFunc)(void (*)(void)) type_name##_init, \\",
    "(GInstanceInitFunc) type_name##_init_adapter, \\")
sub("static void     type_name##_default_init        (TypeName##Interface *klass); \\\n",
    "static void     type_name##_default_init        (TypeName##Interface *klass); \\\n"
    "static void     type_name##_default_init_adapter (gpointer klass, gpointer data G_GNUC_UNUSED) \\\n"
    "{ type_name##_default_init ((TypeName##Interface *) klass); } \\\n")
sub("(GClassInitFunc)(void (*)(void)) type_name##_default_init, \\",
    "(GClassInitFunc) type_name##_default_init_adapter, \\")
open(path, "w").write(s)

# GObject's own root class is registered with a one-argument class_init.
s = open(gobject_c).read()
sub("static void\tg_object_do_class_init\t\t\t(GObjectClass\t*class);",
    "static void\tg_object_do_class_init\t\t\t(GObjectClass\t*class, gpointer class_data);")
sub("g_object_do_class_init (GObjectClass *class)\n{",
    "g_object_do_class_init (GObjectClass *class, gpointer class_data G_GNUC_UNUSED)\n{")
open(gobject_c, "w").write(s)
