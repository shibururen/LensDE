# Material catalogs

The AGF files in this directory supply the materials used for the LensDE benchmark optimizations. Run the preparation script to generate the material database on your machine.

[CDGM AGF](CDGM.AGF) · [PLASTIC2026 AGF](PLASTIC2026.AGF)

## Prepare the benchmark database

After setting up Python, run this command once from the repository's `code/` directory in PowerShell.

```powershell
& .\.venv\Scripts\python.exe catalogs/prepare_materials.py
```

The script generates `code/catalogs/database/catalog_nk.csv` and the 314 CDGM and 69 PLASTIC2026 model files under `data-nk/glass/`. The generated index preserves the material names and order used during optimization. Git ignores this generated directory. A fresh checkout includes the AGF sources and the script.

The default command requires a fresh output directory. Use `--database-root` to prepare another copy. The benchmark commands read the database at the default path. [LensDE setup](../README.md)


## Add a custom material catalog

Use a separate working copy for a custom database. This keeps the recorded catalog index available for benchmark verification.

1. Choose a new library name, such as `MY_GLASS`. Create `catalogs/database/data-nk/glass/my_glass/` under `code/` and add a YAML model for each material. A tabulated model uses wavelength in micrometres and refractive index. Use measured or validated optical data that cover all wavelengths in your project.
2. Add a row for each material to `catalog_nk.csv`. Set `group` to `glass` and make `filename` relative to `data-nk/`, starting with `glass/my_glass/`. LensDE takes the library name from that directory. Record each model's valid wavelength range in micrometres.
3. Restart the workbench after changing the files. Select `MY_GLASS` in the system glass-library settings and assign a material to the desired surface. For a custom project JSON, set `system.glass_libraries` and check `system.glass_search.default_libraries` and any per-surface search restrictions. Existing surfaces retain their assigned materials until you change them.

### Example entry

This example uses illustrative optical values. Save it as `data-nk/glass/my_glass/EXAMPLE_GLASS.yml` inside the database.

```yaml
DATA:
  - type: tabulated n
    data: |
      0.4000000 1.5315
      0.4861327 1.5228
      0.5875618 1.5168
      0.6562725 1.5143
      0.7000000 1.5131
SPECS:
  n_is_absolute: false
  wavelength_is_vacuum: false
  nd: 1.5168
  Vd: 60.8
```

Append the example row to the existing index. The first line below shows the column order.

```csv
group,category_name,category_name_full,reference,name,filename,min_wavelength,max_wavelength,filename_no_ext
glass,MY_GLASS,My glass catalog,MY_GLASS,EXAMPLE_GLASS,glass/my_glass/EXAMPLE_GLASS.yml,0.4,0.7,EXAMPLE_GLASS
```

For a Zemax AGF catalog, convert each material to a supported YAML model before registration. A tabulated model should preserve the source's refractive-index convention, wavelength units, valid range, and dispersion. [Material loading and validation code](../native/src/io/glass_catalog.cpp)
