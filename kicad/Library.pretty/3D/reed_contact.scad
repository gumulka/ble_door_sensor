$fn=100;

module side_leg() {
    color("grey") union() {
        translate([-7,0,0]) rotate([0,90,0]) cylinder(6, d=0.5, center=true);
        translate([-1.5,-0.2,0]) cube([4,0.1,0.5],center=true);
        intersection() {
        translate([-3,0,0]) cube([2,0.5,0.6], center=true);
        translate([-4,0,0]) rotate([0,90,-20]) cylinder(3, d= 0.5, center=true);
        }
    }
}

side_leg();
rotate([0,0,180]) side_leg();

module hull() {
color([0,0.5,0.5,0.3])         union() {
            rotate([0,90,0]) cylinder(h=11.8, d=2.2, center=true);
            translate([-5.9,0,0]) sphere(d=2.2);
translate([5.9,0,0]) sphere(d=2.2);
        }
}

hull();
