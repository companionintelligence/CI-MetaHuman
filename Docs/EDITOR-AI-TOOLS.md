# Editor AI tools

The UE 5.8 editor project vendors two MIT-licensed editor plugins:

- [VibeUE](https://github.com/kevinpbuckley/VibeUE) adds editor automation tools and AgentSkills.
- [VibeMetaHumans](https://github.com/kevinpbuckley/VibeMetaHumans) adds MetaHuman creator,
  animator, identity, Live Link, setup and asset-inspection toolsets.

Both plugins are enabled in `MetaHumanLocalLLM.uproject` for Editor targets only. The packaged
Pixel Streaming game does not load either plugin. Epic's `ModelContextProtocol`, `AllToolsets`,
`ToolsetRegistry` and MetaHuman plugins remain the editor-side host.

## Starting the editor MCP server

Use a UE 5.8 editor and start the native server on loopback. This project can use port 18100 when
the default 8000 is occupied:

```sh
UnrealEditor MetaHumanLocalLLM.uproject \
  -ModelContextProtocolStartServer -ModelContextProtocolPort=18100
```

The server has no authentication and should not be bound to a public or tailnet interface. To
reach an editor on a remote tailnet machine, forward the loopback port over SSH:

```sh
ssh -N -L 18100:127.0.0.1:18100 ci@beta-1
```

VibeMetaHumans registers six toolsets with Unreal's tool registry. VibeUE uses its own tool
registry; `VibeUE.ListTools` prints its registered tools in the editor Output Log. Both plugins
also register AgentSkills from their `Content/Skills` folders when the editor's Python plugin is
available.

## MetaHuman generation requirements

The included project asset `DefaultCompanion` is rigged, but MetaHuman assembly still requires
valid texture sources. Creating a new character from an engine preset produces an editable
MetaHuman Character asset; it still needs Epic's MetaHuman service to auto-rig and download texture
sources before it can be assembled into an actor Blueprint. Sign in to the MetaHuman service in
the editor before running those cloud steps. Do not add unrigged preset copies to a runtime level.

## UE 5.8 compatibility

VibeMetaHumans' JSON map loops use `TPair<FString, ...>` by value. UE 5.8 stores JSON keys as
`UE::TSharedString`, so binding a `TPair<FString, ...>&` created a temporary and failed the
engine's `-Werror=range-loop-construct` build. Keep the loop value form if the plugin source is
updated from upstream.
