# Join

Native modifier (`JOIN`, Python type `JoinModifier`) under Add Modifier > Generate. It is in the
Model column of the Modifier List menu. Source integration only: no build, runtime execution,
or tests were created or run for this addition.

Join concatenates evaluated mesh data into the receiving object's modifier result. It performs
no Boolean, intersection, welding, remeshing or topology cleanup. Overlapping parts remain separate
mesh islands, including coincident vertices. Later modifiers can process the combined geometry.

## Controls and workflow

1. Add **Join** to a mesh object.
2. Use **Add Object** or **Add Collection** to add source entries, then pick their data-blocks.
   Entries can mix both kinds. **Add Selected Objects** adds selected mesh objects individually,
   excluding the receiver and existing direct object entries.
3. Toggle individual sources with the checkbox beside each entry. **X** removes the entry without
   deleting its source object or collection.
4. **Include Original** includes the receiver's current incoming mesh. Disable it for a receiver
   that should contain only sources. With no sources and this option disabled, the result is empty.
5. **Include Child Collections** expands collection sources recursively. Disable it to use only
   direct mesh members of each selected collection.
6. **Piece IDs** writes two face attributes: `object_id`, which is 0 for the receiver and 1, 2,
   3 ... for each source object in order, and `parent_id`, which is the same for every object
   under the same top parent. Collapse with **Pieces** set to **Attribute** can then drop each
   object, or each parent with its children, as one piece. See [Piece ID](PIECE_ID.md).

Objects reached through multiple entries or overlapping collections are included once, by object
identity. Two distinct objects sharing a mesh data-block remain two separately transformed copies.
The receiver is always skipped when it appears inside a source collection, avoiding self-joining.

The source list is owned by the modifier, copied deeply when the modifier is duplicated, and saved
in the .blend file. It supports up to 512 entries and 4096 unique source objects.

## Evaluated geometry and transforms

Source meshes come from dependency-graph evaluation, including their enabled source modifiers.
Position, rotation, scale, parenting and constraints are resolved through each evaluated world
matrix. Source geometry is converted to receiver-local coordinates. Moving the receiver therefore
re-evaluates the local coordinates of referenced geometry to keep it aligned with source world
positions. A singular receiver transform, such as zero scale, retains the original input and warns.
Reflected relative transforms reverse face winding to retain exterior orientation.

The modifier requests mesh attribute data for dependencies and combines it through Blender's existing
geometry join implementation. UV layers, color and other supported attributes are matched by name;
missing values and conflicting types/domains follow the existing geometry-join rules. Vertex-group
names and material slots are combined, and object-linked material overrides are resolved before
joining. Material slots referring to the same material data-block are consolidated.

Source objects are not moved, hidden, deleted or modified. Manage their scene visibility separately
if you want to display only the receiver. The modifier does not filter sources by their visibility.
Only mesh objects are accepted. Non-mesh collection members are ignored. Collection-instance empties
are not expanded, and Geometry Nodes instances must be realized upstream if they are to become part
of a source object's mesh output. This joins evaluated geometry, not independent shape-key systems.

## Dependencies and limits

Geometry and transform dependencies are added for each expanded source object. Collection membership
changes rebuild those relations. Collection geometry is not used as an aggregate dependency, so a
collection can contain the receiver without creating a collection-to-receiver feedback loop.
Direct/indirect chains through Join modifiers that reach the receiver are skipped with a warning;
traversal is limited to 64 levels. Dependencies through other modifier types can still create the
usual Blender dependency cycles and should be avoided.

Unavailable evaluated meshes and cyclic Join sources are skipped. Evaluation is limited to ten
million vertices, twenty million edges, ten million faces and forty million corners. Exceeding a
geometry limit, invalid coordinates or invalid transforms retains the original input with a warning.

## Python API

```
modifier = obj.modifiers.new("Join", 'JOIN')
entry = modifier.sources.new(source_type='OBJECT')
entry.object = other_mesh_object
entry = modifier.sources.new(source_type='COLLECTION')
entry.collection = source_collection
modifier.use_original = True
modifier.use_child_collections = True
# modifier.sources.remove(entry)
```

This is a usage example for the new source API, not an executed test. Existing executables need a
rebuild before the modifier or its API are available. The icon can be regenerated with
`python build_files/utils/make_join_icon.py` (requires Pillow).
