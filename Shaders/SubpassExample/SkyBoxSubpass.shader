{
    "Source" : "SkyBoxSubpass.azsl",
        
    "Definitions": ["QUALITY_LOW_END_TIER1=1", "QUALITY_LOW_END_TIER2=1", "ENABLE_PHYSICAL_SKY=0"],

    "DepthStencilState" : { 
        "Depth" : { "Enable" : false, "CompareFunc" : "GreaterEqual" }
    },

    "DrawList" : "forward",

    "ProgramSettings":
    {
      "EntryPoints":
      [
        {
          "name": "MainVS",
          "type": "Vertex"
        },
        {
          "name": "MainPS",
          "type": "Fragment"
        }
      ]
    },
    "Supervariants":
    [
        {
            "Name": "",
            "AddBuildArguments": {
                "azslc": ["--no-subpass-input"]
            }
        },
        {
            "Name": "SubpassInput",
            "RemoveBuildArguments": {
                "azslc": ["--no-subpass-input"]
            }
        }
    ]
}