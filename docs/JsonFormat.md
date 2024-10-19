# JSON Format

This is the on-disk representation of the symbol database. It is used for
exchanging data between CCC and other programs. See `symbol_database_json.cpp`
and `ast_json.cpp` for the code that currently defines the format.

## Version History

| Format Version | Release | Changes |
| - | - | - |
| 15 | v2.2 | The offset_bytes and bitfield_offset_bits properties of bitfield nodes now represent and are relative to the beginning of the storage unit respectively. |
| 14 | v2.1 | Added stack_frame_size property for function symbols. |
| 13 | v2.0 | Added size_bytes field to all nodes. Renamed data_type_handle property to just data_type (since it's not a handle). |
| 12 | | Added format and application properties to root object. Added hash property to function symbols. |
| 11 | | Lists of indices (instead of begin and end indices) are now used for relationships between symbols. |
| 10 | | Added modules as their own symbol type. Removed the text_address property of source file symbols. |
| 9 | | Added optional is_virtual_base_class property to nodes in base class lists. |
| 8 | | Overhauled the format based on the structure of the new symbol database. An error AST node type has been added. The data, function definition, initializer list, source file and variable AST node types have been removed and replaced. |
| 7 | v1.x | Base classes are now no longer doubly nested inside two JSON objects. Added acccess_specifier property. |
| 6 | | Removed order property. |
| 5 | | Added pointer_to_data_member node type. Added optional is_volatile property to all nodes. Added is_by_reference property to variable storage objects. |
| 4 | | Added optional is_const property to all nodes. Added anonymous_reference type names, where the type name is not valid but the type number is. |
| 3 | | Added optional relative_path property to function definition nodes. |
| 2 | | Added vtable_index property to function type nodes. |
| 1 | | First version. |
