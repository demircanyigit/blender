# Trim

<img src="images/icons/mod_trim.png" width="32" height="32" alt="Trim">

Lays copies of a pattern object along the edges of the mesh. Draw a line of edges, pick a small
piece such as a scallop, a stud or a length of molding, and Trim repeats it along the line, bending
each copy to follow it. Good for scalloped bands, rope edges, moldings, rivets, fences and borders.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the
[Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Build** column.

## Which edges

The edges are joined into paths. A path ends where more or fewer than two edges meet, and a ring of
edges makes a closed path.

- If the mesh has edges without faces, only those are followed. Draw the path as plain edges.
- Otherwise the open borders of the faces are followed, for example the rim of a plate or the
  edge of a panel.

## Settings

- **Pattern**: the object to copy. Its rotation and scale are used, its location isn't.
- **Collection**: several objects to copy, used instead of the Pattern.
  - **Order**: **In Order** uses them one after another, by name. **Random** picks one for each
    copy; **Seed** changes the picks.
- **Fit**:
  - **Repeat**: copies keep the length of the pattern, and as many as fit go along each path.
    They're stretched a little so the last one ends at the end of the path.
  - **Per Edge**: one copy on each edge, stretched to its length.
- **Deform**:
  - **Bend**: copies bend to follow the path. At corners they're cut at the half angle so
    neighbors meet without gaps.
  - **Rigid**: copies stay straight and only turn to follow the path.
- **Length Axis**: the axis of the pattern that runs along the path.
- **Up Axis**: the axis of the pattern that points up.
- **Up**:
  - **Object Z**: up is the Z axis of the object, for paths drawn on the ground.
  - **Faces**: up points out of the faces next to the path, for trim around the border of a
    curved surface.
- **Scale**: size of the pattern.
- **Height**: stretches the pattern along its up axis only.
- **Roll**: turns the pattern around the path.
- **Gap**: space between the copies. Below zero they overlap.
- **Offset**: moves the copies sideways from the path.
- **Start** and **End**: objects placed at the start and the end of open paths, such as end caps.
  With **Per Edge**, they take the first and the last edge.
- **Merge**: joins points of copies that touch, so the band is one piece. **Distance** sets how
  close they must be.
- **Keep Edges**: keeps the original mesh along with the copies.

## In Python

```python
mod = obj.modifiers.new("Trim", 'TRIM')
mod.object = bpy.data.objects["Scallop"]
mod.fit = 'REPEAT'
mod.gap = 0.02
```
