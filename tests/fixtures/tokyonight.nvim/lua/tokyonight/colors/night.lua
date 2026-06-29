local ret = vim.deepcopy(require("tokyonight.colors.storm"))

return vim.tbl_deep_extend("force", ret, {
  fg = "#c0caf5",
})
