# The board database - `bundled/db.json`

`db.json` holds the board, SoC and OS facts that CERF components share.

The file ships inside the CERF installation. It belongs to that CERF
version. A user never edits it.

## What belongs in it

A board, SoC or OS fact belongs here when any other component can use it.
One caller today is enough.

## The shape

Rows carry an `id`, and other rows reference a row by that id.

A field belongs to the table whose id determines it. Every SoC of one family
shares the same core, so the core is a family field.

A row states what is true of the board, and what CERF supports on it.

## String id or enum

**A set that grows when a board or a SoC arrives is a string id.** Boards
and SoCs arrive often. An enum of them lists every member in one header, so
every gate in the tree includes every board, and one new board rebuilds the
whole tree.

**A closed set that a new board never extends stays a C++ enum.** The CPU
architecture and the ROM placing mode are such sets. A new member arrives
only with a new JIT engine or a new boot path. `BoardDatabase` parses the
string from the row once and hands out the enum, so a gate compares an enum
and a wrong string halts at load.

## Where an id constant lives

Each id constant is declared once, in its own header, next to the thing it
names:

- `cerf/boards/<board>/<board_id>_id.h` declares
  `namespace BoardId { inline constexpr std::string_view <Name> = "<id>"; }`
- `cerf/socs/<chip>/<soc_id>_id.h` declares the same in `namespace SocId`.
  A SoC whose only tree is its core strategy keeps the header under
  `cerf/cpu/<core>/`.

Both namespaces are reopenable, so a header opens its namespace again and
adds its one line. The constant name is the PascalCase of the id:
`"nec_mobilepro_900"` becomes `NecMobilepro900`.

A gate includes only the id headers it names:

    return emu_.Get<BoardContext>().GetSocId() == SocId::Sa1110;

**Never gather these constants into one shared header.** One header that
every gate includes puts every board in the dependency set of one
translation unit, which is the rebuild cost the string ids remove.

A template that varies per SoC takes the constant by reference:

    template <uint32_t kBase, const std::string_view& kSoc>

The constant has static storage, so `if constexpr (kSoc == SocId::Imx51)`
compiles.
