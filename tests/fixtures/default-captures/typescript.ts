type Name = string;
interface Greeter { hello(name: Name): void }
const g: Greeter = { hello(name) { console.log(name, 1); } };
g.hello("world");
