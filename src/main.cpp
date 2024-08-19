#include <iostream>
#include "Linking_context.h"


static void Show_link_option_args(const Linking_context::Link_option_args &link_option_args);


int main(int argc, char* argv[])
{
    for(int i = 1 ; i < argc ; i++)
        printf("%s\n",argv[i]);
    printf("\n\n");
    Linking_context::Link_option_args link_option_args ;

    link_option_args.link_machine_optinon = Linking_context::eLink_machine_optinon::elf64lriscv;
    link_option_args.argc = argc-1;
    link_option_args.argv = argv+1;


    Linking_context linking_ctx(link_option_args);

    Show_link_option_args(linking_ctx.link_option_args());
    
    linking_ctx.Link();
    
}


static void Show_link_option_args(const Linking_context::Link_option_args &link_option_args)
{
    printf("out: %s\n", link_option_args.output_file.c_str());
    
    if (link_option_args.library_search_path.size() > 0)
    {
        std::cout << "library path\n";
        for(const auto &path : link_option_args.library_search_path)
        {
            std::cout << path << "\n";
        }
    }
    if (link_option_args.library_name.size() > 0)
    {
        std::cout << "library name\n";
        for(const auto &name : link_option_args.library_name)
        {
            std::cout << name << "\n";
        }
    }

    if (link_option_args.obj_file.size() > 0)
    {
        std::cout << "object file\n";
        for(const auto &obj_file : link_option_args.obj_file)
        {
            std::cout << obj_file << "\n";
        }
    }
}