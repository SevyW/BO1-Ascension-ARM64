// .railway/railway.ts - Railway infrastructure as code for spacemandev.games (web/README.md, "Deploy to Railway").
//
// One service builds web/Dockerfile and serves the game under https://spacemandev.games/bo1.
// Needs the Railway CLI 5.42.1 or newer (`railway upgrade`) and the SDK in this folder (`bun install` here); then,
// from the repository root, linked to the project: `railway config plan` to preview, `railway config apply` to apply.
//
// Deployments are uploads from a working tree (web/scripts/deploy.sh: `railway up` of a staged folder holding the
// prebuilt engine), so the service has no repository source. To build from GitHub instead, add
// `source: github("spacemandev-git/bo1-zombies-decompiled", { branch: "main" })` (import github) once the web
// distribution is on that branch, and set BUILD_ENGINE to "1" (compile the engine in the image) or "0" (no engine):
// a repository context never holds web/engine.

import { defineRailway, project, service } from "railway/iac";

export default defineRailway(() => {
  const web = service("bo1-web", {
    build: {
      builder: "DOCKERFILE",
      // the build context is the repository root (the Dockerfile copies web/, mods/ and data/)
      dockerfilePath: "web/Dockerfile",
      watchPatterns: ["web/**", "mods/**", "data/**", ".railway/**"],
    },
    healthcheck: "/bo1/healthz",
    healthcheckTimeout: 60,
    // The custom domain spacemandev.games (port 8080) is attached with `railway domain spacemandev.games --service
    // bo1-web --port 8080`: Railway configuration cannot register custom domains.
    env: {
      BASE_PATH: "/bo1",
      PORT: "8080",
      // web/Dockerfile build argument: ship the engine the upload holds (web/scripts/deploy.sh stages it)
      BUILD_ENGINE: "prebuilt",
    },
    // Rooms, peers, resume tokens and the packet relay live in this one process's memory. A second replica would
    // split players into separate lobbies (and the relay cannot cross replicas), so this must stay at 1.
    replicas: 1,
  });

  return project("spacemandev-games", { resources: [web] });
});
