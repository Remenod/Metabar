global load_page_directory_extern

load_page_directory_extern:
    mov eax, [esp+4]
    mov cr3, eax

    ret
