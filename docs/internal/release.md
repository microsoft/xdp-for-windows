# Release Process

The following sections are generally for the maintainers of XDP-for-Windows. They describe the process for creating, servicing and publishing new releases. See [Release and Support](../release.md) for the public-facing release, versioning, and support policies.

## Creating a new Release Branch

* [JIT elevate](https://repos.opensource.microsoft.com/orgs/microsoft/repos/xdp-for-windows/jit) to admin, create a `release/major.minor` branch, then revoke JIT.
* Create a PR updating the version number in `main` to the next `major.minor.0` release.
* Add the test artifacts of the new release to the downlevel tests for main and
  any other active release branches.
* Update this release process documentation as necessary.

## Servicing a new Release Branch

* Cherry pick necessary changes to Release/X.Y
* Bump the version in xdp.props XML to X.Y.(Z + 1), where Z is the current latest
* Ensure all changes propagate properly to our internal mirror of XDP-for-windows in ADO: https://microsoft.visualstudio.com/undock/_git/xdp-for-windows
* [JIT elevate](https://repos.opensource.microsoft.com/orgs/microsoft/repos/xdp-for-windows/jit) to admin, create a new tag for version X.Y.(Z + 1) and attach it to the latest commit in the cherry pick by drafting a new release, then revoke JIT.
  
  **Prerelease Validation** Tags may optionally have a `-prerelease1` suffix to verify officially-signed binaries before the final X.Y.Z release. Ensure the naming of any prerelease suffix has the expected precedence, such as by incrementing `1` to `2` for a second prerelease tag.
* Official pipeline in ADO kicks off upon the existence of the new tag and needs manual approval each time: https://microsoft.visualstudio.com/undock/_build?definitionId=134506
* Grab the built artifacts: MSI (for version < 1.3), Nuget Packages + Runtime, and upload them to release X.Y.(Z + 1)
* Grab the test artifacts from the Github CI (ADO does not build XDP tests), and upload them as well
* Upload the XDP + Runtime Nuget pkgs to Nuget.org as well
* Update the downlevel test matrix in main to point to X.Y.(Z + 1) once all artifacts are uploaded
* Update this release process documentation as necessary.
