# Dear ImGui, DX9 backend and ImGuizmo

Copied unchanged from the supplied WarcraftXL tree (`deps/imgui/`): Dear ImGui
1.92.9 WIP (MIT, Omar Cornut) with its `imgui_impl_dx9` backend, and ImGuizmo
1.92.5 WIP (MIT, Cedric Guillemet) including WarcraftXL's rotation hit-area patch
(`RotationFullAxisRings`, separate hit radii, `SetAxisMask`,
`SetGizmoSizeClipSpace`). The Win32 backend is not used: the native client feeds
ImGui its mouse state from SDL input. `LICENSE.txt` is Dear ImGui's licence;
ImGuizmo's MIT notice is in the header of `ImGuizmo.cpp`.

Used only by the game-object editor (`WoW/Source/Ui/Linux/WxlGameObjectEditor.*`).

SHA-256:

- `imgui.cpp`: `235f74b2defda31d0ce87305d63723880e6a7177aa41e9b0bbc0796bbd474c54`
- `imgui_draw.cpp`: `102795e901685f6f75e423e953f481ae68ae32f051f9230734263b792c92e760`
- `imgui_impl_dx9.cpp`: `59d2bc446eb0c7b4d737ccbfa48a474c79eea081e882569f56f263ff55061733`
- `imgui_tables.cpp`: `f6408e749b1224ea9f67aaf5aab69a2a3b2bd7dd0b1939f4afc5c7891eca6a29`
- `imgui_widgets.cpp`: `0cdd6e2d55a1fa6a0c2835de513a137b04c9ad46543881a626de194f3bd73ef3`
- `ImGuizmo.cpp`: `01a2d0ab6d5ba05ec7f1c057d3987b80e536bef8015406824f4be3626864d8a6`
- `imconfig.h`: `f416e21bbe8b97f969259897534781b0e82474b1d04b40898508411c6a8abeda`
- `imgui.h`: `fdf0f9efbebc2c3318ead9f25525cbe2d125382e193e98ce970813a7452c5f75`
- `imgui_impl_dx9.h`: `bd24fa5e9c25b37b926159711d3662d7752d330ddfe54bb54edac484884ec21d`
- `imgui_internal.h`: `c28a830c31c3286515867b89aa74765f05dfce6bc25436ace7d7d284d7e3d418`
- `ImGuizmo.h`: `d62fa9bb59726c065614078320ecf31601b99c7fa5413ca332cfee73847b8523`
- `imstb_rectpack.h`: `7f9bffb8ac5e79d20787fdc20d29d29f7474dca673f64d4a9a8a9f9373e40972`
- `imstb_textedit.h`: `24a8db00354af8f4057417841635a1b6dfd8986f1608336fe6f10d6fd9769aaa`
- `imstb_truetype.h`: `a4131c7a2e324c0625250830af6976adb5732b453f0fc6e8bc5ad70065bc2b79`
