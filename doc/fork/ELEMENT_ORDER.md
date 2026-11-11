# Element Order

<img src="images/icons/mod_element_order.png" width="32" height="32" alt="Element Order">

Reorders the faces or islands of the mesh, keeping the faces of each element together. The Build
modifier and other effects that go by order then run the way you want: randomly, along an axis, or
from a point outward.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Elements** column.

## Settings

- **Element**: What stays together in the new order.
  - **Face**: Each face on its own.
  - **Island**: Each connected piece.
  - **UV Island**: Each UV island.
  - **Material**: All faces with the same material.
  - **Face Set**: Each face set from sculpt mode.
- **UV Map**: UV map for UV islands. The active one when empty.
- **Order**: How to order the elements.
  - **Random**: A random order.
  - **Axis**: Along an axis.
  - **Distance**: Nearest to a point first.
- **Axis**: Axis to order along: X, Y or Z.
- **Point**: Point to measure the distance from, in object space.
- **Seed**: Change it for a different random order.
- **Reverse**: Reverse the order.

## As a node

Geometry Nodes has a **Element Order** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Element Order", 'ELEMENT_ORDER')
```

The Python type is `ElementOrderModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
