// M13/U2 — Open Dungeon Raid (U14 rename) WebView wrapper. Thin Android shell only; all game
// logic lives in the bundled page (milestones/M13/PLAN.md).
pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "DungeonRaidRebuild"
include(":app")
