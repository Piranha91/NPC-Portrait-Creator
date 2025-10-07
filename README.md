# NPC Portrait Creator

This document provides a comprehensive guide on how to interact with the NPC Portrait Creator application, covering both its graphical user interface (GUI) and command-line options.

## 📜 Table of Contents

* [Features](#-features)
* [Basic Interaction (GUI)](#-basic-interaction-gui)
    * [Opening Files](#opening-files)
    * [Camera Controls](#camera-controls)
    * [Managing Data Folders](#managing-data-folders)
* [Advanced Controls (GUI)](#-advanced-controls-gui)
    * [Skeleton Menu](#skeleton-menu)
    * [Image Menu](#image-menu)
    * [Lighting Menu](#lighting-menu)
    * [View Menu](#view-menu)
    * [Inspecting Meshes](#inspecting-meshes)
* [Command-Line Usage](#-command-line-usage)
    * [Basic Example](#basic-example)
    * [All Options](#all-options)
* [Configuration File](#-configuration-file)

---

## ✨ Features

* **High-Quality NIF Rendering**: Renders NetImmerse File Format (`.nif`) models using modern OpenGL.
* **Asset Management**: Loads game assets from loose files or Bethesda Softworks Archives (`.bsa`) by managing multiple data directories with priority overrides.
* **Advanced Lighting**: Supports multiple configurable directional and ambient lights, shadow mapping, and loading/saving of lighting profiles.
* **Interactive Controls**: Features a full suite of interactive camera and lighting controls for precise scene setup.
* **Skeleton Support**: Automatically detects and applies the correct character skeleton (`Male`, `Female`, `Beast`) or allows for a custom skeleton to be loaded.
* **Batch Processing**: A headless mode allows for automated rendering of entire directories of NIF files via the command line.
* **Detailed Inspection**: Right-click on any part of a model to get detailed information about its textures, shader flags, and dismemberment partitions.

---

## 🖥️ Basic Interaction (GUI)

### Opening Files

To load a model, go to **File > Open NIF...**. This will open a standard file dialog where you can select a `.nif` file. When a NIF is loaded, the application will automatically try to locate its root "Data" directory and add it to the list of data folders.

### Camera Controls

The 3D view can be manipulated easily with the mouse and keyboard.

* **Orbit (Rotate)**: **Left-click and drag** the mouse to rotate the camera around the model's center.
* **Pan (Move)**: **Middle-click and drag** the mouse to move the camera target horizontally and vertically.
* **Zoom**: Use the **mouse scroll wheel** to zoom in and out.
* **Axis-Locked Rotation**: Hold down the **`X`**, **`Y`**, or **`Z`** key while left-clicking and dragging to lock camera rotation to that specific axis (Pitch, Yaw, or Roll, respectively).
* **Reset View**: Press **`Ctrl + 0`** to reset the camera to its default position and orientation for the current model.

### Managing Data Folders

The application loads textures and other assets from specified data directories. This is crucial for mods that override vanilla assets.

* **Set Game Data Directory**: Use **File > Set Game Data Directory...** to point the application to your game's main `Data` folder (e.g., `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data`). This directory is always treated as the lowest priority.
* **Add Mod Data Folders**: Use **File > Data Folders > Add Folder...** to add additional data directories, such as the output of a mod manager.
* **Prioritize Folders**: In the **File > Data Folders** menu, you can see the list of active folders. Folders at the **bottom of the list have the highest priority**, meaning their assets will override assets from folders higher up the list. You can use the up/down arrows to reorder them or the "X" button to remove a folder. The model will automatically reload when the folder list is changed.

---

## 🛠️ Advanced Controls (GUI)

### Skeleton Menu

The Skeleton menu controls the underlying bone structure used for posing skinned models like bodies and faces.

* **Auto-Detection**: When a NIF is loaded, the program inspects its shapes and textures for keywords like "female" or "argonian" to automatically select the appropriate skeleton.
* **Manual Override**: You can manually select a different skeleton (e.g., `Female`, `Male Beast`) from the menu at any time.
* **Load Custom Skeleton**: If you have a custom skeleton file, you can load it using **Skeleton > Load Custom Skeleton...**.

### Image Menu

* **Save PNG...**: Saves the current view as a PNG image. The resulting PNG contains embedded metadata with all the settings used to generate it, such as lighting, camera position, and the NIF file's hash.
* **Process Directory...**: This powerful feature allows for batch rendering. You will be prompted to select an input directory containing NIF files and an output directory for the PNGs. The application will then load each NIF, automatically frame it using the current "mugshot" settings, and save a corresponding PNG.

### Lighting Menu

This menu gives you full control over the scene's lighting.

* **Lighting Profiles**: You can **Load** and **Save** complete lighting setups as `.json` files. This is useful for creating and reusing consistent lighting environments.
* **Interactive Light Editing**:
    1.  Check the **"Edit Directional Lights"** box to show interactive arrows in the 3D view representing each directional light source. The application may automatically zoom out to ensure all arrows are visible.
    2.  **Drag an arrow with the left mouse button** to change the light's direction.
    3.  **Hold `X`**, **`Y`**, or **`Z`** while dragging to lock rotation to a single axis.
    4.  **Right-click an arrow** to open a context menu where you can fine-tune its `Intensity` and `Color`, or `Delete` and `Duplicate` the light.
* **Normal Map Corrections**:
    * **Enable Normal Map Hack**: This checkbox automatically corrects the orientation of different types of normal maps to ensure lighting appears correctly. It applies a -90 degree pitch rotation to standard tangent-space normals and a +90 degree pitch to eye normals, while leaving model-space normals untouched.
    * **Per-Mesh Normal Rotation**: For fine-tuning, you can expand the "Adjust Mesh Normals" menu to manually set a pitch and yaw rotation for the normals of any individual mesh part.

### View Menu

This menu contains options for adjusting the camera, model visibility, and rendering quality.

* **Camera**: Adjust the camera's **Field of View (FOV)** for a wider or narrower perspective. You can also manually input `Pitch`, `Yaw`, and `Roll` values.
* **Mesh Parts**: This section lists every individual shape in the loaded NIF model (e.g., `Face`, `Hair`, `Eyes`). You can toggle the visibility of each part by checking or unchecking its box.
* **Texture Slots**: Control which texture maps are active in the shader. You can disable maps like `Normal Map` or `Specular Map` to see their effect on the final render. Checking the adjacent `Debug` box will render that texture directly, bypassing all lighting, which is useful for inspection.
* **Compatibility**: Contains options to emulate older rendering engines. "Suppress Specular on Vertex Colors" disables specular highlights on any mesh that also has vertex colors, preventing a "washed out" look on some older assets.

### Inspecting Meshes

To get detailed information about a specific part of the model, simply **right-click on it** in the 3D view. A "Mesh Information" popup will appear, showing:
* The **name** of the shape (`Face`, `Hair`, `Eyes`, etc.).
* A list of all **dismemberment partitions** it belongs to (e.g., `SBP_30_HEAD`).
* The **filenames** of its assigned textures.
* A list of all active **shader flags** (e.g., `SLSF1_Skinned`, `SLSF2_Double_Sided`) that control how it's rendered.

---

## ⌨️ Command-Line Usage

The application can be run entirely from the command line without a GUI, which is ideal for automation and batch processing.

### Basic Example

This command loads a NIF file, applies a custom skeleton, sets the game data directory, adds a mod's data directory, and saves a 512x512 PNG image.

```bash
NPCPortraitCreator.exe --headless --file "C:\MyMods\MyNPC\meshes\actors\character\facegendata\facegeom\MyMod.esp\00001234.nif" --output "D:\Renders\MyNPC.png" --gamedata "C:\Skyrim\Data" --data "C:\MyMods\MyNPC" --skeleton "C:\MyMods\MySkeletons\skeleton.nif" --imgX 512 --imgY 512
