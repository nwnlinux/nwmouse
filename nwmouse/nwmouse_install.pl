#!/usr/bin/perl -w

# Dweebie installation program. 
#
# - David Holland zzqzzq_zzq@hotmail.com - 7/7/03
# - David Holland david.w.holland@gmail.com - 5/3/06

use strict; 

use vars qw( %nwnhash $size $command );
use vars qw( $location );
use vars qw( @array $line $offset );

use vars qw( $gcc $cflags $ldflags );

use vars qw( $dir $ndir );

use vars qw( $machine $x86_64 );

if( exists( $ENV{"CC"} )) {
        $gcc = $ENV{"CC"};
} else {
        $gcc = "gcc -m32";
}

if( exists( $ENV{"CFLAGS"} )) {
        $cflags = $ENV{"CFLAGS"};
} else {
        $cflags = "";
}

if( exists( $ENV{"LDFLAGS"} )) {
        $ldflags = $ENV{"LDFLAGS"};
} else {
        $ldflags = "";
}

$machine = `/bin/uname -m`; chomp($machine);
if( $machine =~ "x86_64" ) {
	$x86_64 = "-I/emul/ia32-linux/usr/include -L/emul/ia32-linux/usr/lib -L/emul/ia32-linux/lib -L/lib32 -L/usr/lib32";
} else {
	$x86_64 = "";
}

foreach $dir ( ".", "nwmouse" ) {
	if ( -f "$dir/nwmouse.c" ) {
		$ndir = $dir;
		last;
	}
}

$command = sprintf("%s %s %s -I%s/libdis -g -fPIC -shared -Wl,-soname,libdisasm.so %s/libdis/libdis.c %s/libdis/i386.c -o %s/libdis/libdisasm.so", 
		$gcc, $cflags, $x86_64, $ndir, $ndir, $ndir, $ndir );
printf("NOTICE: NWMouse: Executing: %s\n", $command); 
system($command); 

$command = sprintf("%s %s %s -shared -g -I/usr/include/libelf -I%s/libdis -o %s/nwmouse.so %s/nwmouse_cookie.c %s/nwmouse_link.S %s/nwmouse_link2.S %s/nwmouse.c %s -ldl -lelf -Xlinker -L/usr/X11R6/lib -lXcursor -lSDL", 
		$gcc, $cflags, $x86_64, $ndir, $ndir, $ndir, $ndir, $ndir, $ndir, $ldflags );

printf("NOTICE: NWMouse: Executing: %s\n", $command); 
system($command); 

# install symlink
if( $ndir eq "." ) {
	chdir("..");
}
symlink("nwmouse/nwmouse.so", "nwmouse.so"); 

printf("NOTICE: NWMouse: Please check for errors above\n"); 
printf("NOTICE: NWMouse: nwmouse executable built. Please modify your nwn startup command to\n"); 
printf("NOTICE: NWMouse: set LD_PRELOAD to include 'nwmouse.so', before executing nwmain.\n"); 


exit(0); 
