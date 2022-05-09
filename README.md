# articulate-pocketsphinx-go
Articulate's version of pocketsphinx in Go as of 4th of May 2022.  

The module xyz_plus is the Go wrapper for xyzpocketsphinx_continuous and xyzpocketsphinx_batch. 

This version requires our xyz's version of pocketsphinx (https://github.com/DavidBarbera/articulate-pocketsphinx) installed in the system with logging disabled, which is currently the main branch.

caller_plus is a module to test xyz_plus but exists in another repository.

## Requirements
Go  
compiler: g++ 
libraries: -lxyzsphinxbase, -lxyzsphinxad, -lxyzpocketsphinx (These will be available after installing articulate-pocketsphinx in the system)    

