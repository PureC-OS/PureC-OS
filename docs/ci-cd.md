# PureC OS CI/CD

The `Build, Test and Release PureC OS` workflow runs for pushes and pull
requests targeting `main` or `Dev`. It can also be started manually.

The pipeline has four gates:

1. Host unit tests compile and exercise CPU discovery, kernel string helpers,
   program aliases, and path normalization.
2. The complete kernel, modules, userspace, and bootable ISO are built. The
   external repositories are checked out at the commits recorded by the root
   repository.
3. The same ISO is booted by QEMU with 1, 2, and 4 virtual CPUs. Each runner
   checks the serial log for topology discovery, AP startup where applicable,
   and scheduler startup. Logs are retained as workflow artifacts.
4. A successful push to `main` publishes `V1.2.<workflow run number>`. A manual
   workflow run publishes only when `publish_release` is enabled.

## Caches

The x86_64 ELF cross compiler is stored in the GitHub Actions cache. Its cache
key includes the host OS, architecture, compiler versions, and the hash of the
toolchain build script. It is rebuilt only when that exact cache is absent.
Cargo downloads and build outputs use a separate cache keyed by the Rust lock
file and toolchain configuration.

To force a compiler rebuild, change
`.github/scripts/build-cross-toolchain.sh` or delete its cache in the GitHub
Actions cache settings.

## Release rollback

If a published build must be withdrawn, delete its GitHub release and tag. With
GitHub CLI this is:

```sh
gh release delete V1.2.<number> --cleanup-tag
```

Fix the problem and rerun the workflow from the corrected commit. The next run
gets a new version number and must pass all four gates before publication.
