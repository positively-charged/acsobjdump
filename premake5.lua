solution 'acsobjdump'
   configurations 'release'
   language 'C'

   buildoptions {
      '-Wall',
      '-Werror',
      '-Wno-error=switch',
      '-Wno-unused',
      '-std=c99',
      '-pedantic',
   }

   flags {
      'Symbols',
   }

   project 'acsobjdump'
      location 'build'
      kind 'ConsoleApp'
      targetdir '.'
      targetname 'acsobjdump'

      files {
         '*.c',
      }