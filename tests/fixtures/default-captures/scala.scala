class Greeter { def hello(name: String = "world"): Unit = println(s"hello $name ${1}") }
object Main extends App { new Greeter().hello() }
