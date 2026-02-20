# Changelog

## Unversioned

- Major: Rebase on Chatterino7 instead of Chatterino (956b241e5dd02dc6e00f9fe807ebb7ed53dbf9ea) (2879b11a2e466da13e4f2ffb47b9425d6e12df72) (a3e4463d45514474675de5b62bb91c0564471aff) (8a0a103448b4ba5f8c65a6b52dbb1a2fe8d06e39) (5f95858eb0928c63a632dbf9fd203fbb37f39b4f) (63b3019323cf468ec6eabfb13259686cf63bf480)
- Minor: Added anon mode where user joins chat as a logged out as user, so user doesn't appear in user's joined or parted, but still able to make API calls and send messages as signed in user. Disables the event sub which has some useful mod features or if not signed in tells you whether your message was held by automod. (6f85d48fcc31006cb91a75efb2eb441805b4cbdc)

## 2.5.4 Bajerino

- Major: Refactor crypto and remove legacy crypto code. (484a8826cfbb2b9ab5cbcfc424975f881504e361)
- Major: Add custom badge for tomas. (a4cf35e4f3622725ab13e524a8723d5c77c1b1b1)
- Minor: Change placement of crypto toggle button and change checkbox styling to use unlock and lock svg instead. (653c95ffc95fd959ebbed22a0cca780e6d495a67) (6442faf8ac6e8b7286cbde1030a12f773b305762)
- Minor: Replace `chatterino` with `bajerino` in GUI. (6cff983e72338e741a24fa2842d8c4b749f447a9) (a9312d45fe3e719abafa5dd36f587751dccdfd87) (677a93bb56c13d8cb3174dc0b26e56c0e78a80c2)
- Minor: Replace icon with custom Bajerino Icon. (59365670f1c6c0ccb3d28d2be5c6acb31525c02b)
- Minor: Change from 🔓 to a lock png based on svg and make it a proper badge. (73f305b36c088589b11a4a6c8d1266fef575c886) (5468c3e4958f0e6bb01ebf0ded985ff0e5a29baf)
- Minor: Add hotkey for encryption. (8264e58adabe3eeecd265926f573f043070ff195)
- Minor: Add mode to use Chatterino directory for settings. (d13986b8f9329e6094040a1dd45e16d08d5fcfc0)
- Minor: Add option to toggle using the lock icon for the encryption toggle checkbox. (76137c32a0a4b92b44e87a4ff8724279fdc27b11)
- Dev: Disabled Doxygen in CMAKE because it was annoying me. (3ff20a840bd8e6741ded6b4202bbd17ddab0828b)
- Dev: Merge upstream (from 20b92ea0ca186f4a543ea85d5b8153a7c92bb2d0). (5a51771ebbcdfc99767105cf7007d856c76d27fc)
- Dev: Change channel states map from using `std::map` and `std::string` with `QHash` and `QString` respectively. Implement rapidjson serializer and deserializer for types. (e0f721b453f6864fa2b03981cadb3bf705f841df) (3cd199ead134a31655504795fafa69601a2e353c)
- Dev: Disable Dependabot because it was annoying and upstream can handle that. (4bd172c61d51222e318ab144255ab83643e69c31)
- Dev: Revert hack in 47ffbe9586f3d070281793fd37b1a31c37cb0ab3 and properly fix by changing folder name in Conan config. Disable chatterino updater. Rename from `bajerino2` to `bajerino` in CI. (d55d0548c9148967d2b3f83a46e7bd51c07a7bf1)
- Dev: Merge upstream (from 33918261460041fdda2ee0c80b4a1a754b75842b). (0e50b16941099902a3a5695526748dfabb62d202)
- Dev: Merge upstream (from ae3089b35cedefcb4b2e31f070fedc9a996a9a68). (4959a07e6b2bccad8edb7d703e054ba603abac56)
- Dev: Replace custom line edit widget with upstream implementation. Modify to allow auto-trimming as people keep unintentionally entering passwords with whitespace. (551710748e51f7ac48f931777b9276240aa38c4f)

## 2.5.3.2

- Major: Per channel encryption toggle. (143e49d790652530412193af6c91994be4272bbe)
- Bugfix: fixed crash when closing thread popout. (4080885996535d2fde8fd86fdc020e6930f9be42)
- Dev: Add `<QCheckbox>` to SplitInput to fix build. (6fe6dc046d7ed17489c34ca230a0ce1631bdc606)

## 2.5.3.1

- Major: Rebase onto stable release 2.5.3 (3f3a31db4ddf3b634eff78b3201f15e6b8ce7b14). (35d74115c6be03f04dccd9b2f66dc8584ebbeab5)
- Dev: Make a GitHub repo.
- Dev: Rename values in CI from `chatterino` to `bajerino`. (2fd526352792bffa231ad9ade09793b811ff1bd6)
- Dev: Enable workflows to enable building and releasing on GitHub.
- Dev: Fix windows produced artifact by manually copying in OpenSSL libs. (47ffbe9586f3d070281793fd37b1a31c37cb0ab3)

## 2.5.2.1

- Dev: Rebase onto stable release 2.5.2 (f53d92c77a304307256ca56e4e07ceb0471f44d4). (176961eac4661dfdddcb744ee6e28259dd57cc5d)
- Dev: Add Python tool that reimplements crypto logic. (6660b10029f37da01730f8d941aec82853d6ee9d)
- Dev: Remove static from crypto variables as they are in anon namespace. (749a3aa3a16374e98748c828d3fb41b7b753e825)

## 2.5.2-beta.1.5

- Minor: Add decrypt indicator in `Replying To` bar. (940d7a6a6b7a9fa55a13cfa9631243514f60ab8f) (5c44a29f41471b5045464d6ff6295e108d4dfe0b)
- Fix: Include `<QLineEdit>` in GeneralPageView to fix build. (33193e7ea9e16477e89531b0f206d5d0b052349d)
- Dev: Merge upstream (from 2212ec0bbb3952aef3d49233b394b590fbd65c57). (5c44a29f41471b5045464d6ff6295e108d4dfe0b)
- Dev: Merge upstream (from 8b7d7f9d710835f22b15c9339d366d8aff690519). (addae3cc0d3567f09df4341c012cda1f5d4eaf9c)

## 2.5.2-beta.1.4

- Major: Remove prefix that was used to indicate that message should be decrypted. Instead just attempt decryption and catch if it fails. (d0257a4eb695e3fd5a1c16c755a4ec12cb123e3f)
- Minor: Check if first character within encrypted character range to avoid unnecessary effort in decryption. (6e40caf1d11fc92638bdcc4496d7f7e0d51fb8d1)
- Dev: Wrap internal crypto variables and functions in anonymous namespace. (d73b02990b96e661008edf7bb992d3ce9ef90df7)
- Dev: Merge upstream (from 4f1a0ad5a6c5b678ecfbc3a8c6ec6b646e569e8a). (76be0134a005a0694744a6cb73d8fb43e5e1729d)

## 2.5.2-beta.1.3

- Major: Reimplement crypto using OpenSSL instead of CryptoPP. OpenSSL is already a dependency, so no need to use a seperate library. (1b4f806f88810271e5bc4e935e26f3a9009bb3b1)
- Major: Switch from AES ECB to AES CBC to avoid pattern recognition. (1b4f806f88810271e5bc4e935e26f3a9009bb3b1)
- Major: Encode encrypted messages as Chinese characters instead of Base64. (1b4f806f88810271e5bc4e935e26f3a9009bb3b1)
- Minor: Still support decrypting legacy AES ECB messages if the encryption password is 24 bytes in length. (1b4f806f88810271e5bc4e935e26f3a9009bb3b1)
- Minor: Change Encryption Key to Encryption Password. (1b4f806f88810271e5bc4e935e26f3a9009bb3b1)
- Dev: Remove CryptoPP dependency. (b646c77fb9debb34f1d8e7f1137b0b86c790f7d5)

## 2.5.2-beta.1.2

- Major: Change decypted indicator from 🔒 to 🔓. (bb9641983a6787270c51f57c8db4d0ae3e7d285e)
- Minor: Move Bajerino Settings to own section in menu. (1a5367681513dc63974964e4ff946bf7d64c2f58)
- Minor: Move decrypt lock icon from message to badge. (bb9641983a6787270c51f57c8db4d0ae3e7d285e)
- Bugfix: Still display messages that failed to decrypt. (7589747f6b7b607245db0324962f6fffea91c711)

## 2.5.2-beta.1.1

- Major: Rename from `bajterino` to `bajerino`. (c1dfe6ca0d82ae9f4838736a1d3f0eff1f4f1c0d)
- Major: Rebase onto a much more recent commit `fae3e7a`. (c1dfe6ca0d82ae9f4838736a1d3f0eff1f4f1c0d)
- Major: BajTV Provider and tree history removed. (c1dfe6ca0d82ae9f4838736a1d3f0eff1f4f1c0d)
- Major: Squashed and reimplement parts of crypto to better fit into existing codebase and structure. (c1dfe6ca0d82ae9f4838736a1d3f0eff1f4f1c0d)

## 2.5.2-nightly.2

- Major: Implement message encryption with CryptoPP. Messages are encrypted/decrypted with AES EBC and a 24 byte/192 bit key. ([31098e5800f9ee5bb2707ab204aecac2dff740f5](https://gitlab.com/bajtv/bajtv-chatterino2/-/commit/31098e5800f9ee5bb2707ab204aecac2dff740f5))

## 2.5.2-nightly.1

- Major: Implement BajTV provider. ([9f45bc50fdf7577c2b919779aaf5aa809f38e2c0](https://gitlab.com/bajtv/bajtv-chatterino2/-/commit/9f45bc50fdf7577c2b919779aaf5aa809f38e2c0))
