# ROM bundle repositories

The launcher installs and updates ROMs from **bundle repositories**: static HTTP hosting that
publishes a `manifest.json` catalog and one archive per device. CERF ships with one repository
configured. You can add others, or run your own.

A repository is plain file hosting. The launcher reads `<repository-url>/manifest.json`, lists what
it finds, and fetches archives over HTTPS. No account, no API, no server-side code.

## What the launcher does with a repository

- **Catalog.** Every enabled repository's manifest is fetched and merged into one list in **Download
  ROMs**. Two repositories may publish a device under the same name; both stay installable.
- **Metadata before download.** A manifest entry carries that device's whole `cerf.json`, so the
  board, OS, screen size and notes are known without fetching a single byte of ROM.
- **Updates.** An installed device records the repository it came from. When the manifest's archive
  hash changes, the launcher offers an update. When only the metadata changed, it rewrites
  `cerf.json` and does not re-download the ROM.
- **Sizes and integrity.** Entries carry the compressed and unpacked size, and a SHA-256 that is
  verified after download.

### Managing repositories

Open **Download ROMs**, then **Sources...**. Add a repository by its base URL
(`https://example.com/bundles`, not the path to the manifest), tick it to enable it, or delete it.
The list lives in `cerf.json` next to `cerf.exe`, under `bundle_repositories`.

## Running your own repository

The contract, the toolchain and the documentation are at
**[gweslab/bundles](https://github.com/gweslab/bundles)**. It packs a ROM tree into archives, builds
`manifest.json`, and publishes the result.

## Copyright removal

### The main repository

If you hold the copyright to content on the repository CERF ships with, or represent the holder,
send a removal request to **cerfabuse@dz3n.net**. Requests are acted on as soon as possible and the
content is deleted.

### Any other repository

The CERF project does not host, control or audit third-party repositories and cannot remove anything
from them. A repository publishes its own contact address, and that address is optional. Where it
exists, it is available in two places:

- **The launcher.** **Copyright removal**, in the Download ROMs window and in the download
  confirmation, lists every configured repository with its contact.
- **`manifest.json`.** The top-level `abuse_email` field.

A repository that publishes no address gives you no contact. Address its operator or its host
directly.
