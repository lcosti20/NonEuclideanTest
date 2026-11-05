# Video play-by-play: Non-Euclidean Worlds Engine

Video: [Non-Euclidean Worlds Engine — CodeParade](https://www.youtube.com/watch?v=kEB11PQ9Eo8\&utm_source=chatgpt.com)\
Approximate duration: 5 minutes, 14 seconds\
Subject: A demonstration of a custom 3D rendering engine that creates impossible-looking spaces using non-Euclidean geometry and seamless spatial transitions.

## Full Transcript

> "I've been working on a rendering engine for non-euclidean worlds. It's a little tricky to explain. So I'll just show you some of the things you can do before I go into how it works. First example is this tunnel. You can see it's really long from the outside... ...but it's actually really short on the inside. And then this tunnel is the opposite. It appears short, but it takes a long time to travel through it. The shortest path between two points is not necessarily a straight line anymore. Here's another example of a house. See if you notice anything suspicious as I walk through it. Did you catch it? This four-room house actually only has three rooms, red, green, and blue. This demo surprised me a lot because I originally thought it would be super jarring but actually everyone I show this to doesn't notice it at all unless I tell them that something is off. I guess our brains just aren't used to thinking this way. Here's the same house, but with the opposite twist. There's actually six rooms in this house. And in fact, I could cram an infinite amount of space into any finite space. Here's another room. There's just a pillar on one side and an object on the other. But if we walk around the pillar, we actually end up in another room. The object isn't changing. We're actually in another unique space. If I walk around again we enter a third room. And this cycle repeats, clockwise and counter-clockwise. And notice how you can see both objects at once. This is a weird one, we're on a hill that's sloped up but to get up the hill we can go down the tunnel, and end up on top. It's also an example of your grandpa's stories about walking uphill both ways, maybe he just lived in a non-Euclidean plane of existence. Here's where things get really interesting. We've got a tunnel that's too small to pass through. But as I walk through THIS tunnel, my scale also changes. Still not small enough to get through, but if I go around one more time... I can finally fit through. I can go the other way around too, to keep getting bigger. But eventually I won't be able to fit into the tunnel anymore. So how does all this work? Well, it's basically the same as the game Portal but the portals have no seams and there's often more than two of them There are several surfaces that are actually solid geometry. It's just that I draw a texture on top of it that's actually a virtual camera rendering what it would have seen from that perspective And when you walk through it, it teleports you seamlessly to where you're supposed to end up and the illusion is complete. The rendering is a lot harder than it sounds though. Each surface has to recursively render surfaces behind it conditionally depending on if it's occluded or not from each perspective. I originally was trying to do this in Unity, but I'm pretty sure it's impossible without lower level access to the rendering, even the scriptable pipeline didn't work for me. So I was forced to create my own rendering engine in OpenGL. Why do I keep writing engines... Anyway, the last thing I wanted to talk about are the use cases. Other than just for puzzle games. This is actually incredibly useful for virtual reality. You see, normally for room scale VR, you're limited to a small area to walk around in. You can't really explore larger open worlds without breaking the illusion, like teleporting. But this engine can compress entire levels to fit into a single room. Here I have a single 20 foot by 20 foot room that you can walk around in and explore. But it actually contains a volume four times larger. And although I haven't actually tested this in VR. I'm entirely convinced that this illusion feels just as real as playing from the screen I've uploaded the source and executable to github. So if you're curious you can try out this demo. Thanks for watching, and I'll see you all next time."

## 1. High-level summary

The video demonstrates a 3D engine in which the normal rules of spatial geometry do not necessarily apply.

The environment looks like an ordinary collection of rooms, corridors, tunnels, and objects. However, moving through it produces unusual spatial effects:

- A tunnel can be much shorter to travel through than its apparent exterior length.
- A building can contain fewer or more rooms than its layout suggests.
- Walking around an object can transport the player into another part of the environment.
- Moving downhill through a tunnel can result in arriving at a higher elevation.
- Passing through a tunnel can change the player's apparent size.

The video first demonstrates these effects and then explains how they are implemented using portal-like rendering and camera transformations.

The central idea is that the geometry presented to the player does not have to correspond to a globally consistent Euclidean 3D world.

## 2. Chronological play-by-play

The following timestamps are approximate and refer to the original video's timeline.

### 00:00–00:26 — Portal that compresses distance

- The video begins by demonstrating a tunnel connecting two areas.
- From the outside, the tunnel appears relatively long.
- When traveling through it, however, the player reaches the other side much sooner than the visible geometry would suggest.
- The distance traveled through the tunnel is effectively compressed.
- The result is that the shortest route through the environment is not necessarily the route that appears shortest visually.

Spatial rule being demonstrated: Traversing a particular region can cover less world-space distance than its apparent geometry implies.

### 00:26–00:42 — Portal that expands distance

- The video demonstrates the inverse of the previous effect.
- A tunnel appears to provide a relatively short connection between two locations.
- Once the player enters it, the actual traversal takes considerably longer than expected.
- The interior effectively stretches the distance between its entrance and exit.
- The apparent shortcut is therefore slower than its exterior appearance suggests.

Spatial rule being demonstrated: A visually short path can contain more traversal distance than expected.

### 00:42–01:21 — A missing room

- The demonstration moves to a building-like environment containing several connected rooms.
- The player navigates through the structure.
- The arrangement of rooms does not behave like an ordinary building.
- A room that would normally be expected to exist in the apparent layout is effectively missing.
- The player can navigate through the building while experiencing a spatial arrangement that cannot be reconciled with an ordinary flat floor plan.

Spatial rule being demonstrated: The relationship between rooms and their apparent physical arrangement is not globally consistent.

### 01:21–01:41 — Extra rooms

- The next demonstration reverses the previous type of spatial anomaly.
- Instead of having fewer rooms than expected, the environment effectively contains additional rooms.
- The player can traverse a room arrangement that appears to have more interior space than its exterior layout would permit.
- The structure behaves like a larger or differently connected building without requiring a correspondingly larger conventional floor plan.

Spatial rule being demonstrated: The number of reachable interior spaces does not have to correspond to the apparent physical layout.

### 01:41–02:13 — Walking around a column causes teleportation

- The scene changes to a room containing a prominent column or central obstruction.
- The player begins walking around the column.
- As the player continues around it, the surrounding environment changes in an unexpected way.
- Rather than simply completing an ordinary circuit around the column, the player transitions into another spatial configuration.
- Continuing to walk around the obstruction can lead through a sequence of different spatial regions.
- The environment can even produce the impression that multiple spatial configurations coexist around the same apparent location.

Spatial rule being demonstrated: A continuous walking path can connect to a different location or spatial configuration without a conventional visible doorway.

### 02:13–02:42 — A downward tunnel leads upward

- The demonstration introduces a sloping landscape with a tunnel running through it.
- The tunnel appears to descend as the player enters it.
- However, following the tunnel results in reaching a higher elevation.
- The apparent direction of travel and the actual change in elevation contradict one another.
- The same environment can therefore make a downward-sloping passage function as an upward route.

Spatial rule being demonstrated: The visual orientation of a passage does not necessarily correspond to the direction of movement in the larger environment.

### 02:42–03:23 — A portal that changes object and player scale

- The next example introduces a tunnel that changes scale.
- The player approaches and travels through the passage.
- As the player progresses, their apparent size changes.
- Objects and surrounding geometry consequently appear to change relative size.
- Traveling through the tunnel in one direction causes the player to become smaller.
- Traveling back through it produces the inverse effect, making the player larger again.
- The tunnel effectively acts as a spatial transformation that changes scale rather than merely relocating the player.

Spatial rule being demonstrated: A continuous traversal can change the scale of the player and the apparent dimensions of the surrounding environment.

The sequence of demonstrations, including the compressed and expanded tunnels, room-count anomalies, column traversal, elevation reversal, and scaling tunnel, is corroborated by a timestamped reproduction of the original video.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://rutube.ru\&sz=32)

RUTUBE+1



### 03:23–03:54 — Explanation of how the engine works

- After demonstrating the spatial effects, the video transitions into explaining their implementation.
- The creator compares the underlying technique to the portal mechanics in the game Portal.
- The basic idea is to connect different regions of space through seamless transitions.
- Rather than physically constructing one enormous, globally consistent 3D environment, the engine manipulates how the player sees and moves between spatial regions.
- Virtual cameras are used to render what should be visible through these connections.
- When the player approaches or crosses a transition, the engine updates the viewpoint and spatial relationship so that the transition appears continuous.
- The technique allows two visually separate areas to behave as though they are directly connected.

Technical concept: Portal-based rendering, virtual cameras, and spatial transformations.

### 03:54–04:33 — Rendering challenges and implementation

- The creator discusses the difficulty of implementing this rendering system.
- One particularly challenging aspect is recursive rendering.
- A portal may show another area containing additional portals, which themselves need to display other areas.
- The renderer must therefore determine how deeply to render these nested views and avoid infinite recursion.
- The creator explains that Unity was not suitable for the particular implementation because of limitations in accessing the necessary lower-level rendering functionality.
- Instead, the engine was implemented from scratch using C++ and OpenGL.

Technical concept: Custom rendering pipeline with recursive portal rendering.

The associated source repository confirms that this is a Windows-based 3D rendering engine written in C++ using OpenGL.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+1



### 04:33–05:14 — VR applications and conclusion

- The creator discusses how this technology could be useful in virtual reality.
- One particularly interesting application is allowing a relatively small physical room to contain a much larger virtual environment.
- For example, a physical room measuring approximately 20 feet by 20 feet could accommodate a virtual environment several times larger.
- The player could explore that environment by walking naturally, while the engine uses spatial transformations to make the virtual environment fit within the physical tracking area.
- This provides a potential solution to the limited physical walking space available in conventional VR setups.
- The video concludes by pointing viewers toward the project resources and its implementation.

Technical concept: Using non-Euclidean spatial transformations to create large explorable virtual environments inside physically constrained spaces.

## 3. Consolidated list of demonstrated spatial mechanics

This section extracts the actual behaviors demonstrated, independently of the video's presentation order.

| ID | Mechanic                              | Observable behavior                                                        |
| -- | ------------------------------------- | -------------------------------------------------------------------------- |
| M1 | Distance compression                  | A long-looking tunnel is short to traverse.                                |
| M2 | Distance expansion                    | A short-looking tunnel is long to traverse.                                |
| M3 | Missing room                          | The interior has fewer rooms than its apparent layout suggests.            |
| M4 | Additional rooms                      | The interior has more rooms than its apparent layout suggests.             |
| M5 | Spatial looping                       | Walking around a column transitions between different spaces.              |
| M6 | Elevation inversion                   | A downward-sloping tunnel leads to a higher elevation.                     |
| M7 | Scale transformation                  | Traversing a tunnel changes the player's scale.                            |
| M8 | Recursive portal views                | Portals render other spatial regions, potentially containing more portals. |
| M9 | Physical-to-virtual space compression | A small physical VR area accommodates a larger virtual environment.        |

## 4. Technical interpretation

For an LLM or someone implementing a similar system, the important distinction is between three different concepts:

A. Geometry

What the player sees: walls, floors, tunnels, rooms, objects, and portals.

B. Traversal

What happens when the player moves: teleportation, distance compression, room transitions, elevation changes, and scale changes.

C. Rendering

How the engine constructs the image: virtual cameras, portal views, transformations, and recursive rendering.

These three things do not necessarily have to describe the same globally consistent Euclidean space.

For example, a tunnel can have:

- One apparent length when viewed externally.
- A different traversal distance when entered.
- A transformed destination when exited.
- A different relationship between the player's scale and the surrounding environment.

This separation is the key architectural idea behind the demonstrations.

## 5. LLM-ready condensed description

The following is a standalone description suitable for feeding into another LLM as context.

# Non-Euclidean Worlds Engine — Video Description

A demonstration of a custom 3D rendering engine that implements non-Euclidean spatial environments.

The video demonstrates the following mechanics in chronological order:

1. **Distance compression (00:00):** A tunnel appears long externally but is significantly shorter to traverse internally.
2. **Distance expansion (00:26):** A tunnel appears short externally but requires significantly more traversal distance internally.
3. **Missing room (00:42):** A building's interior contains fewer rooms than its apparent spatial layout would suggest.
4. **Additional rooms (01:21):** A building's interior contains more rooms than its apparent exterior layout would permit.
5. **Spatial looping (01:41):** Walking around a central column transitions the player between different spatial configurations.
6. **Elevation inversion (02:13):** A downward-sloping tunnel leads to a higher elevation.
7. **Scale transformation (02:42):** Traversing a tunnel changes the player's scale. Reversing direction reverses the transformation.
8. **Implementation explanation (03:23):** The engine uses portal-like spatial connections and virtual cameras to render different regions of space.
9. **Rendering architecture (03:54):** Recursive portal rendering creates implementation challenges. The creator uses a custom C++/OpenGL engine rather than Unity.
10. **VR application (04:33):** Spatial transformations can allow a physically small room to contain a much larger explorable virtual environment.

## Core architectural principle

The visible geometry, traversal rules, and rendering transformations are not required to correspond to one globally consistent Euclidean coordinate system.

The engine achieves impossible-looking spatial effects by connecting different spatial regions through seamless transitions and transforming the player's viewpoint and position as they traverse those connections.

The objective is to create environments that appear visually continuous while exhibiting unusual spatial topology, distance relationships, orientation, and scale.

Related implementation: [HackerPoet/NonEuclidean — GitHub source repository](https://github.com/HackerPoet/NonEuclidean?utm_source=chatgpt.com).

Accuracy note: I reconstructed the timeline using the video-linked material and timestamped reproductions because YouTube itself did not expose its video stream to my retrieval. The description captures the documented sequence and mechanics, but it is not a frame-by-frame transcription of every visual or spoken detail.