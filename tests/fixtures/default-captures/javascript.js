import fs from "fs";
class Greeter { hello(name = "world") { console.log(`hi ${name}`, 1); } }
new Greeter().hello();
