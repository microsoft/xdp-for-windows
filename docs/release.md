# Release and Support

All active development occurs on GitHub in the `main` branch. When it is time to release, the code is forked into a release branch where it is considered stable and will generally only receive servicing for security and bug fixes. New releases may be created at any time, but likely won't be more frequent than 2 or 3 times a year.

## Branches

XDP-for-Windows has two primary types of branches:

* **Main** - Main is the active development branch, and receives security and bug fixes just the same as the release branches. However, it may experience breaking changes as we develop new features. For this reason, it is recommended that no dependencies be taken on the main branch.

* **Release** - Release branches only receive security and bug fixes, and are considered stable. There should be no breaking changes in these branches, and they can be used for stable products.

## Tags

Tags are used for the actual releases. Tags are created from the release branches, and are used to mark the code at a specific point in time. Tags are immutable, and should not be changed once created.

## Versioning

XDP-for-Windows uses [Semantic Versioning](https://semver.org/) for versioning releases. The version number is defined in the release branch name and can also be found in the [xdp.props](../src/xdp.props) file. SemVer specifies that:

- **Major** version changes indicate **breaking changes**. This means that code that worked in a previous major version may not work in the new major version. This is generally due to API changes, but can also be due to changes in behavior.

- **Minor** version changes indicate **new features**. This means that code that worked in a previous minor version will continue to work in the new minor version. This is generally due to new APIs or features being added.

- **Patch** version changes indicate **bug fixes**. This means that code that worked in a previous patch version will continue to work in the new patch version.

XDP-for-Windows uses the `release/(major).(minor)` naming convention for all release **branches**. For example, the first official release branch will be `release/1.0`.

XDP then uses the `v(major).(minor).(patch)` naming convention for all **tags**. For example, the first official release will be `v1.0.0`.

## Lifecycle and Support Policies

XDP-for-Windows follows a model where only the latest release of each major version is supported, for 18 months. In other words, each release is supported for 18 months, unless a new minor or patch version replaces it, at which point the 18 month window resets.

This means that when a new major version is released, the previous major version will still be supported for 18 months from its last release date. This allows customers to upgrade and consume breaking changes at their own pace, while still receiving security and bug fixes for the previous version.

It also means that when a new minor version is released, the previous minor version (of the matching major version) is no longer supported, and that customers will be expected to consume any servicing fixes via the new minor version releases. Because they are only minor version changes, they will not require any code changes from customers.

> **Note** - XDP-for-Windows plans to try to never to have any breaking changes, and therefore never have any major version changes. However, this policy is in place just in case.

### Example

For example, if `v1.0` is released on January 1st, 2024 and then `v2.0` is released on July 1st, 2024, then `v1.0` will be supported until July 1st, 2025. Then, if `v2.1` is released on January 1st, 2025, then `v2.0` is no longer supported. Fixes will only go into `v2.1` and since `v2.1` has no breaking changes, customers that were on `v2.0` can upgrade to `v2.1` without any code changes.

# Official Releases

> **Note** - There are no official releases of XDP-for-Windows yet! This section is currently a placeholder.

The following are the official releases of XDP-for-Windows.

| Version | Fork Date | Release Date | End of Support |
|   --    |     --    |       --     |       --       |
| v1.0.0 | TBD | TBD | TBD |

# Release Process

The following sections are generally for the maintainers of XDP-for-Windows. They describe the process for creating, servicing and publishing new releases.

## Creating a new Release Branch

> **Note** - TODO

## Servicing a new Release Branch

> **Note** - TODO

## Publishing a new Release

> **Note** - TODO
