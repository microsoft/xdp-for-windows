# CI Agent Pools

General documentation for how to use 1ES hosted pools: [1es-hosted-pool-for-testing.md](https://mscodehub.visualstudio.com/undocking/_git/undocking?path=/docs/1es-hosted-pool-for-testing.md&_a=preview).

## 1ES Hosted Pools used by XDP

We own several 1ES Hosted Pools in the [`CoreOS_LIOF_WindowsXDP_dev`](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67) subscription. These pools automatically provision VMs as needed for CI tests using managed images. We also have 1ES GitHub Actions pools; the GitHub pools share the same 1ES images as the Azure Pipelines pools.

| Pool Name                  | Configuration   | Resource Group                                                                                                                                                                        |
|----------------------------|-----------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| XDP-CI-1ES-Functional-2    | functional.json | [xdp-ci-functional-rg-2](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-functional-rg-2)   |
| XDP-CI-1ES-Spinxsk-2       | spinxsk.json    | [xdp-ci-spinxsk-rg-2](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-spinxsk-rg-2)         |
| xdp-ci-functional-gh       | functional.json | [xdp-ci-functional-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-functional-gh-rg) |
| xdp-ci-functional-arm64-gh | functional.json | [xdp-ci-functional-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-functional-gh-rg) |
| xdp-ci-spinxsk-gh          | spinxsk.json    | [xdp-ci-spinxsk-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-spinxsk-gh-rg)       |
| xdp-ci-spinxsk-arm64-gh    | spinxsk.json    | [xdp-ci-spinxsk-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-spinxsk-gh-rg)       |
| xdp-ci-fuzz-gh             | none            | [xdp-ci-fuzz-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-fuzz-gh-rg)             |
| xdp-ci-perf-gh             | none            | [xdp-ci-perf-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-perf-gh-rg)             |
| xdp-ci-perf-arm64-gh       | none            | [xdp-ci-perf-gh-rg](https://ms.portal.azure.com/#@microsoft.onmicrosoft.com/resource/subscriptions/db4b9e5a-88c6-424a-8c94-728d1ce2ec67/resourceGroups/xdp-ci-perf-gh-rg)             |

## 1ES Images

We use a mix of standardized and customized images across our agent pools.

Some images are shared across Azure Pipelines and GitHub pools; for example, `WS2019-Functional` is used by both `XDP-CI-1ES-Functional-2` in Azure Pipelines and `xdp-ci-functional-gh` in GitHub.

Some images are shared across pool types; for example, the `none` configuration is used by both `xdp-ci-fuzz-gh` and `xdp-ci-perf-gh`.

| Image Name                    | Configuration        | RESOURCE GROUP         |
|-------------------------------|----------------------|------------------------|
| WS2019-Functional             | functional.json      | xdp-ci-functional-rg-2 |
| WS2022-Functional             | functional.json      | xdp-ci-functional-rg-2 |
| WSPrerelease-Functional       | functional.json      | xdp-ci-functional-rg-2 |
| WSPrerelease-arm64-Functional | functional.json      | xdp-ci-functional-rg-2 |
| WS2019-Spinxsk                | spinxsk.json         | xdp-ci-spinxsk-rg-2    |
| WS2022-Spinxsk                | spinxsk.json         | xdp-ci-spinxsk-rg-2    |
| WSPrerelease-Spinxsk          | spinxsk.json         | xdp-ci-spinxsk-rg-2    |
| WSPrerelease-arm64-Spinxsk    | spinxsk.json         | xdp-ci-spinxsk-rg-2    |
| WS2019                        | none                 | xdp-ci-perf-gh-rg      |
| WS2022                        | none                 | xdp-ci-perf-gh-rg      |
| WSPrerelease                  | default.json         | xdp-ci-perf-gh-rg      |
| WSPrerelease-arm64            | default.json         | xdp-ci-perf-gh-rg      |

## Updating Image Configurations

Some of our 1ES managed images have extra configuration applied during image creation. These configurations are stored in JSON files at [.azure/agents](/.azure/agents). Whenever a configuration is updated, the author should:

1. Create a standalone PR consisting only of the JSON changes and corresponding [prepare.machine.ps1](/tools/prepare-machine.ps1).
2. Once the PR is approved, the author should navigate to the [Azure portal](https://ms.portal.azure.com/) and update each 1ES image with the new JSON. It may take several hours for the 1ES images to be rebuilt with the new configuration.
3. The author should then re-run GitHub actions on the pipelines and validate the new configuration has taken effect.
4. The author completes the PR.
