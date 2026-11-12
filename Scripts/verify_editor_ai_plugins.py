"""Interactive UE 5.8 smoke check for the bundled Vibe editor integrations."""

import json
import unreal
import vibemetahumans

EXPECTED_TOOLSETS = {
    "VibeMetaHumans.MetaHumanAnimatorService",
    "VibeMetaHumans.MetaHumanCharacterService",
    "VibeMetaHumans.MetaHumanIdentityService",
    "VibeMetaHumans.MetaHumanLiveLinkService",
    "VibeMetaHumans.MetaHumanObjectService",
    "VibeMetaHumans.MetaHumanSetupService",
}

toolsets = set(vibemetahumans.list_toolsets())
missing = sorted(EXPECTED_TOOLSETS - toolsets)
status = vibemetahumans.status()
unreal.log("CI_EDITOR_AI_TOOLSETS {}".format(json.dumps(sorted(toolsets))))
unreal.log("CI_EDITOR_AI_STATUS {}".format(json.dumps(status, sort_keys=True)))

if missing:
    raise AssertionError("VibeMetaHumans toolsets missing: {}".format(", ".join(missing)))
if not status.get("success"):
    raise AssertionError("VibeMetaHumans setup status failed")
