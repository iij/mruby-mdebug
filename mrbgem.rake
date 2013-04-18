MRuby::Gem::Specification.new('mruby-mdebug') do |spec|
  spec.license = 'MIT'
  spec.authors = 'Internet Initiative Japan Inc.'
  spec.bins = %w(mrdb)
  spec.linker.libraries << ['termcap', 'readline']
end
