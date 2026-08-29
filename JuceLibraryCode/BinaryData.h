/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   veslogo_png;
    const int            veslogo_pngSize = 91859;

    extern const char*   rom_svg;
    const int            rom_svgSize = 395;

    extern const char*   artwork_svg;
    const int            artwork_svgSize = 500;

    extern const char*   floppy_svg;
    const int            floppy_svgSize = 318;

    extern const char*   cdrom_svg;
    const int            cdrom_svgSize = 364;

    extern const char*   harddisk_svg;
    const int            harddisk_svgSize = 386;

    extern const char*   InterRegular_ttf;
    const int            InterRegular_ttfSize = 411640;

    extern const char*   InterSemiBold_ttf;
    const int            InterSemiBold_ttfSize = 419744;

    extern const char*   OFL_txt;
    const int            OFL_txtSize = 4380;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 9;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
