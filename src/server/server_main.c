
int main(const int argc, char **argv) {
    server_context ctx;
    ctx = init_context();
    ctx.argc = argc;
    ctx.argv = argv;
}